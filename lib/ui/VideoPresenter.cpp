#include "VideoPresenter.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <new>

#include "AudioHooks.h"
#include "AudioTap.h"
#include "St77916Driver.h"
#include "VideoShelf.h"

namespace drehklang::ui {

namespace {

constexpr int64_t kFrameUs = 40000;  // 25 fps, what scripts/convert-video.sh writes.
// What the I2S DMA holds ahead of the jack: 16 buffers of 256 frames.
constexpr uint32_t kDmaLatencyFrames = 16 * 256;
constexpr BaseType_t kPushCore = 0;
constexpr UBaseType_t kPushPriority = 3;
constexpr uint32_t kPushStackBytes = 4096;
// Dropped frames read per tick at most, so a long catch-up never stalls
// the loop.
constexpr int kMaxDropsPerTick = 8;
constexpr uint32_t kStripeWaitMs = 200;
// With headphones connected, core 0 also feeds the second chip, and the
// sound broke up on the jack and the headphones alike while the push task
// ran there at any priority (U4WDH underruns, 2026-10-13: 14-22 per 30 s
// at prio 2-3; at prio 1 only 8.6 pictures a second got through). So with
// headphones every second picture is shown and the loop pushes it itself:
// read, decode and push take ~44 ms of the 80 ms a picture then has.
constexpr uint32_t kHeadphoneFrameStep = 2;
// The headphones play ~150 ms after the jack (ADR 0027); the picture
// waits for them.
constexpr int64_t kHeadphoneDelayUs = 150000;

bool headphonesConnected() {
  const auto *tap = drivers::bluetoothTap();
  return tap && tap->forwarding();
}

}  // namespace

bool VideoPresenter::startTask() {
  if (freeStripes_) return true;
  freeStripes_ = xQueueCreate(kStripes, sizeof(int8_t));
  filledStripes_ = xQueueCreate(kStripes, sizeof(int8_t));
  if (!freeStripes_ || !filledStripes_) return false;
  return xTaskCreatePinnedToCore(&VideoPresenter::pushTask, "video", kPushStackBytes, this,
                                 kPushPriority, nullptr, kPushCore) == pdPASS;
}

// Internal DMA RAM pushes ~7 % faster (CircuitPresenter); PSRAM still works.
bool VideoPresenter::allocate() {
  const size_t bytes = kStripeWidth * kStripeRows * sizeof(uint16_t);
  for (int8_t i = 0; i < kStripes; ++i) {
    void *pixels = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!pixels) pixels = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    stripes_[i].pixels = static_cast<uint16_t *>(pixels);
    if (!pixels) return false;
    xQueueSend(freeStripes_, &i, 0);
  }
  frame_ = static_cast<uint8_t *>(heap_caps_malloc(kMaxFrameBytes, MALLOC_CAP_SPIRAM));
  void *memory = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM);
  if (memory) decoder_ = new (memory) JPEGDEC();
  return frame_ && decoder_;
}

void VideoPresenter::release() {
  xQueueReset(freeStripes_);
  xQueueReset(filledStripes_);
  for (Stripe &stripe : stripes_) {
    heap_caps_free(stripe.pixels);
    stripe.pixels = nullptr;
  }
  if (decoder_) {
    decoder_->~JPEGDEC();
    heap_caps_free(decoder_);
    decoder_ = nullptr;
  }
  heap_caps_free(frame_);
  frame_ = nullptr;
  heap_caps_free(stillPixels_);
  stillPixels_ = nullptr;
  file_.reset();
}

bool VideoPresenter::open(const std::string &title, const video::VideoResume &from, bool play) {
  if (isOpen()) close();
  if (!startTask()) return false;
  const std::string base = std::string(video::kVideoFolder) + "/" + title;
  fs::File file = SD_MMC.open((base + ".avi").c_str());
  if (!file) return false;
  file_ = std::make_unique<drivers::SdRawFile>(std::move(file));
  if (!avi_.open(*file_) || !allocate()) {
    release();
    return false;
  }
  title_ = title;
  start_ = from;
  if (!avi_.seekFrame(from.frame)) {
    start_ = video::VideoResume{};
    avi_.seekFrame(0);
  }
  shownFrame_ = start_.frame;
  frameSize_ = 0;
  soundStarted_ = false;
  state_ = State::Cued;
  if (play) {
    resume();
  } else {
    captureStill();
  }
  return true;
}

void VideoPresenter::close() {
  if (!isOpen()) return;
  waitForStripes();
  audio_.endVideoSound();
  glue_.setPanelOwnedByGame(false);
  release();
  state_ = State::Closed;
}

bool VideoPresenter::startSound() {
  drivers::resetOutputFrames();
  const std::string path = std::string(video::kVideoFolder) + "/" + title_ + ".m4a";
  soundStarted_ = audio_.playVideoSound(path, start_.soundPosition);
  return soundStarted_;
}

void VideoPresenter::takePanel() {
  glue_.setPanelOwnedByGame(true);
  clearPanel();
}

void VideoPresenter::pause() {
  if (state_ != State::Playing) return;
  audio_.pause();
  waitForStripes();
  captureStill();
  glue_.setPanelOwnedByGame(false);
  state_ = State::Paused;
}

void VideoPresenter::resume() {
  if (state_ == State::Cued) {
    takePanel();
    if (!startSound()) {
      glue_.setPanelOwnedByGame(false);
      return;
    }
  } else if (state_ == State::Paused) {
    takePanel();
    audio_.resume();
  } else {
    return;
  }
  state_ = State::Playing;
}

void VideoPresenter::jumpTo(uint32_t seconds) {
  if (!isOpen()) return;
  const uint32_t last = avi_.frameCount() == 0 ? 0 : avi_.frameCount() - 1;
  restartSoundAt(std::min(seconds * kFramesPerSecond, last));
}

// The sound is reopened at the byte the jump's time maps to, and the
// picture follows from the matching frame; the clock starts again there.
void VideoPresenter::restartSoundAt(uint32_t frame) {
  if (state_ == State::Playing) waitForStripes();
  if (!avi_.seekFrame(frame)) return;
  const std::string path = std::string(video::kVideoFolder) + "/" + title_ + ".m4a";
  start_ = video::VideoResume{
      drivers::Esp32AudioI2SDriver::soundOffsetAt(path, frame / kFramesPerSecond), frame};
  shownFrame_ = frame;
  frameSize_ = 0;
  if (state_ != State::Playing) takePanel();
  if (!startSound()) {
    glue_.setPanelOwnedByGame(false);
    state_ = State::Cued;
    return;
  }
  state_ = State::Playing;
}

// Microseconds of the film's sound that have reached the jack since it
// started at start_.
int64_t VideoPresenter::clockUs() const {
  const uint32_t rate = drivers::decoderRate();
  const uint32_t frames = drivers::outputFrames();
  if (rate == 0 || frames <= kDmaLatencyFrames) return 0;
  return static_cast<int64_t>(frames - kDmaLatencyFrames) * 1000000 / rate;
}

bool VideoPresenter::readNext() {
  frameSize_ = avi_.nextFrame(frame_, kMaxFrameBytes, frameIndex_);
  return frameSize_ > 0;
}

bool VideoPresenter::tick() {
  if (state_ != State::Playing) return true;
  if (!audio_.isRunning()) return false;  // The sound ended: so has the film.
  const bool headphones = headphonesConnected();
  if (headphones != pushOnLoop_) {
    waitForStripes();
    pushOnLoop_ = headphones;
  }
  const int64_t delay = headphones ? kHeadphoneDelayUs : 0;
  for (int drops = 0; drops <= kMaxDropsPerTick; ++drops) {
    if (frameSize_ == 0 && !readNext()) return false;
    if (headphones && frameIndex_ % kHeadphoneFrameStep != 0) {
      frameSize_ = 0;
      continue;
    }
    const int64_t due = static_cast<int64_t>(frameIndex_ - start_.frame) * kFrameUs + delay;
    const int64_t now = clockUs();
    if (now < due) return true;
    if (now <= due + kFrameUs) {
      decodeBuffered();
      return true;
    }
    frameSize_ = 0;  // A frame late already: dropped.
  }
  return true;
}

void VideoPresenter::decodeBuffered() {
  if (frameSize_ == 0) return;
  if (decoder_->openRAM(frame_, static_cast<int>(frameSize_), &VideoPresenter::queueStrip)) {
    x0_ = (drivers::kLcdHorRes - decoder_->getWidth()) / 2;
    height_ = decoder_->getHeight();
    y0_ = (drivers::kLcdVerRes - height_) / 2;
    decoder_->setUserPointer(this);
    decoder_->setMaxOutputSize(std::min(decoder_->getWidth(), kStripeWidth) / 16);
    decoder_->decode(0, 0, 0);
    decoder_->close();
  }
  shownFrame_ = frameIndex_;
  frameSize_ = 0;
}

// JPEGDEC hands over a full-width strip; it is copied into a free stripe
// and queued, so the next one decodes while this one is pushed.
int VideoPresenter::queueStrip(JPEGDRAW *block) {
  auto *self = static_cast<VideoPresenter *>(block->pUser);
  const int rows = std::min(block->iHeight, self->height_ - block->y);
  const int width = std::min(block->iWidth, kStripeWidth);
  if (rows <= 0) return 1;
  int8_t index = 0;
  if (!self->pushOnLoop_ &&
      xQueueReceive(self->freeStripes_, &index, pdMS_TO_TICKS(kStripeWaitMs)) != pdTRUE) {
    return 0;
  }
  Stripe &stripe = self->stripes_[index];
  for (int row = 0; row < rows; ++row) {
    std::memcpy(stripe.pixels + row * width, block->pPixels + row * block->iWidth,
                static_cast<size_t>(width) * sizeof(uint16_t));
  }
  stripe.x = self->x0_ + block->x;
  stripe.y = self->y0_ + block->y;
  stripe.width = width;
  stripe.rows = rows;
  if (self->pushOnLoop_) {
    self->glue_.pushStripe(stripe.x, stripe.y, stripe.width, stripe.rows, stripe.pixels, false);
  } else {
    xQueueSend(self->filledStripes_, &index, portMAX_DELAY);
  }
  return 1;
}

// Core 0. Never prints (AGENTS.md).
void VideoPresenter::pushTask(void *param) {
  auto *self = static_cast<VideoPresenter *>(param);
  for (;;) {
    int8_t index = 0;
    xQueueReceive(self->filledStripes_, &index, portMAX_DELAY);
    const Stripe &stripe = self->stripes_[index];
    self->glue_.pushStripe(stripe.x, stripe.y, stripe.width, stripe.rows, stripe.pixels, false);
    xQueueSend(self->freeStripes_, &index, portMAX_DELAY);
  }
}

// Until the task has pushed every queued stripe: the caller is about to
// draw itself, or to free them.
void VideoPresenter::waitForStripes() {
  const uint32_t start = millis();
  while (uxQueueMessagesWaiting(freeStripes_) < kStripes && millis() - start < kStripeWaitMs) {
    delay(1);
  }
}

void VideoPresenter::clearPanel() {
  static uint16_t black[drivers::kLcdHorRes * 8] = {};
  for (int y = 0; y < drivers::kLcdVerRes; y += 8) {
    glue_.pushStripe(0, y, drivers::kLcdHorRes, 8, black, false);
  }
}

// The frame shown, decoded once more into a buffer LVGL can draw from.
// The film reads on from the frame after it.
void VideoPresenter::captureStill() {
  if (!stillPixels_) {
    stillPixels_ = static_cast<uint16_t *>(heap_caps_malloc(
        kStillMaxWidth * kStillMaxHeight * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    if (!stillPixels_) return;
  }
  frameSize_ = 0;
  if (!avi_.seekFrame(shownFrame_) || !readNext()) return;
  if (decoder_->openRAM(frame_, static_cast<int>(frameSize_), &VideoPresenter::copyToStill)) {
    const int width = std::min(decoder_->getWidth(), kStillMaxWidth);
    const int height = std::min(decoder_->getHeight(), kStillMaxHeight);
    still_.header.cf = LV_IMG_CF_TRUE_COLOR;
    still_.header.w = static_cast<uint32_t>(width);
    still_.header.h = static_cast<uint32_t>(height);
    still_.data_size = static_cast<uint32_t>(width * height * sizeof(uint16_t));
    still_.data = reinterpret_cast<const uint8_t *>(stillPixels_);
    decoder_->setUserPointer(this);
    decoder_->decode(0, 0, 0);
    decoder_->close();
  }
  frameSize_ = 0;
}

int VideoPresenter::copyToStill(JPEGDRAW *block) {
  auto *self = static_cast<VideoPresenter *>(block->pUser);
  const int width = static_cast<int>(self->still_.header.w);
  const int height = static_cast<int>(self->still_.header.h);
  for (int row = 0; row < block->iHeight && block->y + row < height; ++row) {
    const int columns = std::min(block->iWidth, width - block->x);
    if (columns <= 0) break;
    std::memcpy(self->stillPixels_ + (block->y + row) * width + block->x,
                block->pPixels + row * block->iWidth, static_cast<size_t>(columns) * 2);
  }
  return 1;
}

video::VideoResume VideoPresenter::position() {
  if (!soundStarted_) return start_;
  return video::VideoResume{audio_.videoSoundPosition(), shownFrame_};
}

}  // namespace drehklang::ui
