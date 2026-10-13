#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <JPEGDEC.h>
#include <lvgl.h>

#include <cstdint>
#include <memory>
#include <string>

#include "AviReader.h"
#include "Esp32AudioI2SDriver.h"
#include "LvglGlue.h"
#include "SdRawFile.h"
#include "VideoResume.h"

namespace drehklang::ui {

// Plays a film from /Videos (ADR 0031): <title>.avi's Motion-JPEG pictures
// on the panel, <title>.m4a's sound through the audio driver. The sound is
// the clock -- a picture is shown when the DAC has been handed that much
// sound, and dropped when it is already a frame late.
//
// Pictures decode on the caller (the main loop, core 1) into stripes that
// a task on core 0 pushes to the panel. That task runs above the audio
// library's decoder (prio 2): the sound has ~280 ms of buffers to catch up
// from, a late stripe has none. With it below, it got only the gaps and
// half the frames were dropped (measured 2026-10-13). Like Circuit, it owns
// the panel while open (LvglGlue::setPanelOwnedByGame) and asks for the
// full CPU clock.
class VideoPresenter {
 public:
  enum class State { Closed, Cued, Playing, Paused };

  VideoPresenter(LvglGlue &glue, drivers::Esp32AudioI2SDriver &audio) : glue_(glue), audio_(audio) {}

  // Opens the film at `from`. With `play` it takes the panel and plays;
  // without, it waits Cued, its picture in still() for LVGL to show.
  bool open(const std::string &title, const video::VideoResume &from, bool play);
  void close();
  State state() const { return state_; }
  bool isOpen() const { return state_ != State::Closed; }

  // Playing -> Paused: the sound stops, the picture shown becomes still()
  // and the panel goes back to LVGL, which draws the controls over it.
  void pause();
  // Cued or Paused -> Playing: the panel is the film's again.
  void resume();
  // Plays on from `seconds` into the film (clamped to it).
  void jumpTo(uint32_t seconds);
  // Shows the picture that is due. False once the film has ended.
  bool tick();
  // Where to resume: the sound heard and the picture shown with it.
  video::VideoResume position();
  uint32_t seconds() const { return shownFrame_ / kFramesPerSecond; }
  uint32_t durationSeconds() const { return avi_.frameCount() / kFramesPerSecond; }
  // The picture the film stands at while Cued or Paused, for an lv_img.
  const lv_img_dsc_t *still() const { return stillPixels_ ? &still_ : nullptr; }

 private:
  static constexpr int kStripes = 2;
  static constexpr int kStripeWidth = 320;
  static constexpr int kStripeRows = 16;
  static constexpr size_t kMaxFrameBytes = 64 * 1024;
  static constexpr uint32_t kFramesPerSecond = 25;
  static constexpr int kStillMaxWidth = 320;
  static constexpr int kStillMaxHeight = 240;

  struct Stripe {
    uint16_t *pixels = nullptr;
    int x = 0, y = 0, width = 0, rows = 0;
  };

  bool startTask();
  bool allocate();
  void release();
  bool startSound();
  int64_t clockUs() const;
  bool readNext();
  void decodeBuffered();
  void waitForStripes();
  void clearPanel();
  void takePanel();
  void captureStill();
  void restartSoundAt(uint32_t frame);
  static int queueStrip(JPEGDRAW *block);
  static int copyToStill(JPEGDRAW *block);
  static void pushTask(void *self);

  LvglGlue &glue_;
  drivers::Esp32AudioI2SDriver &audio_;
  State state_ = State::Closed;
  std::string title_;

  std::unique_ptr<drivers::SdRawFile> file_;
  video::AviReader avi_;
  JPEGDEC *decoder_ = nullptr;
  uint8_t *frame_ = nullptr;
  size_t frameSize_ = 0;  // Read and not yet shown or dropped; 0 when none.
  uint32_t frameIndex_ = 0;
  Stripe stripes_[kStripes];
  QueueHandle_t freeStripes_ = nullptr;
  QueueHandle_t filledStripes_ = nullptr;
  int x0_ = 0, y0_ = 0, height_ = 0;
  // With headphones the loop pushes the stripes itself (core 1), and core 0
  // is left to the sound and the link to the second chip.
  bool pushOnLoop_ = false;

  video::VideoResume start_;  // Where the sound started.
  uint32_t shownFrame_ = 0;
  bool soundStarted_ = false;
  uint16_t *stillPixels_ = nullptr;
  lv_img_dsc_t still_{};
};

}  // namespace drehklang::ui
