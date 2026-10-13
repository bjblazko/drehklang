#include "VideoPresenter.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <new>

#include "AudioHooks.h"
#include "CpuClock.h"
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
  file_.reset();
}

bool VideoPresenter::open(const std::string &title, const video::VideoResume &resume, bool play) {
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
  start_ = resume;
  if (!avi_.seekFrame(resume.frame)) {
    start_ = video::VideoResume{};
    avi_.seekFrame(0);
  }
  shownFrame_ = start_.frame;
  frameSize_ = 0;
  soundStarted_ = false;
  drivers::setFullCpuSpeed(true);
  glue_.setPanelOwnedByGame(true);
  clearPanel();
  state_ = State::Cued;
  // The picture the film stands at, before any sound.
  if (readNext()) decodeBuffered();
  if (play) togglePause();
  return true;
}

void VideoPresenter::close() {
  if (!isOpen()) return;
  waitForStripes();
  audio_.endVideoSound();
  glue_.setPanelOwnedByGame(false);
  drivers::setFullCpuSpeed(false);
  release();
  state_ = State::Closed;
}

bool VideoPresenter::startSound() {
  drivers::resetOutputFrames();
  const std::string path = std::string(video::kVideoFolder) + "/" + title_ + ".m4a";
  soundStarted_ = audio_.playVideoSound(path, start_.soundPosition);
  return soundStarted_;
}

void VideoPresenter::togglePause() {
  switch (state_) {
    case State::Cued:
      if (startSound()) state_ = State::Playing;
      break;
    case State::Playing:
      audio_.pause();
      state_ = State::Paused;
      waitForStripes();
      drawPauseMark(true);
      break;
    case State::Paused:
      waitForStripes();
      drawPauseMark(false);
      audio_.resume();
      state_ = State::Playing;
      break;
    case State::Closed:
      break;
  }
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
  for (int drops = 0; drops <= kMaxDropsPerTick; ++drops) {
    if (frameSize_ == 0 && !readNext()) return false;
    const int64_t due = static_cast<int64_t>(frameIndex_ - start_.frame) * kFrameUs;
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
  if (xQueueReceive(self->freeStripes_, &index, pdMS_TO_TICKS(kStripeWaitMs)) != pdTRUE) return 0;
  Stripe &stripe = self->stripes_[index];
  for (int row = 0; row < rows; ++row) {
    std::memcpy(stripe.pixels + row * width, block->pPixels + row * block->iWidth,
                static_cast<size_t>(width) * sizeof(uint16_t));
  }
  stripe.x = self->x0_ + block->x;
  stripe.y = self->y0_ + block->y;
  stripe.width = width;
  stripe.rows = rows;
  xQueueSend(self->filledStripes_, &index, portMAX_DELAY);
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

// Two bars below the picture while paused: a still frame alone could be a
// hang. White on the black band, inside the round bezel.
void VideoPresenter::drawPauseMark(bool shown) {
  constexpr int kBarWidth = 8;
  constexpr int kBarHeight = 24;
  constexpr int kGap = 8;
  static uint16_t white[kBarWidth * kBarHeight];
  static uint16_t black[kBarWidth * kBarHeight] = {};
  std::fill(std::begin(white), std::end(white), 0xFFFF);
  const int top = std::min(drivers::kLcdVerRes - 70, (drivers::kLcdVerRes + height_) / 2 + 20);
  const int left = drivers::kLcdHorRes / 2 - kGap / 2 - kBarWidth;
  const uint16_t *pixels = shown ? white : black;
  glue_.pushStripe(left, top, kBarWidth, kBarHeight, pixels, false);
  glue_.pushStripe(left + kBarWidth + kGap, top, kBarWidth, kBarHeight, pixels, false);
}

video::VideoResume VideoPresenter::position() {
  if (!soundStarted_) return start_;
  return video::VideoResume{audio_.videoSoundPosition(), shownFrame_};
}

}  // namespace drehklang::ui
