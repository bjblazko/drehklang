#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>

#include "CircuitRenderer.h"
#include "CircuitScene.h"
#include "LvglGlue.h"

namespace drehklang::ui {

// Puts Circuit on the panel (ADR 0030). A task on core 0 renders a frame
// in 16-row stripes into two alternating buffers while the main loop on
// core 1 pushes the previous stripe over QSPI: the bus is the bottleneck
// (~32 ms for a full frame, measured 2026-10-06), so the drawing hides
// behind it.
//
// While open, the game owns the panel and LVGL's own flushes are dropped
// (LvglGlue::setPanelOwnedByGame). Everything big is allocated on open and
// freed on close, so the game costs no memory while it is not showing.
class CircuitPresenter {
 public:
  explicit CircuitPresenter(LvglGlue &glue) : glue_(glue) {}

  // Takes the panel. False if the memory is not there; the caller then
  // leaves the panel to LVGL.
  bool open();
  // Gives the panel back. Waits for a frame in flight to finish first.
  void close();
  bool isOpen() const { return renderer_ != nullptr; }

  // Draws one frame of `scene` and pushes it; blocks for about one bus
  // frame (~25 ms). Main loop only.
  void present(const games::CircuitScene &scene);

 private:
  static constexpr int kStripeRows = 16;
  static constexpr int kStripes =
      (games::CircuitRenderer::kSize + kStripeRows - 1) / kStripeRows;
  static constexpr uint32_t kStripeTimeoutMs = 200;
  // Every this many frames the picture is also kept for SCREENSHOT: the
  // copy costs ~3 ms a frame, and a screenshot a quarter second old is
  // as good as a fresh one.
  static constexpr uint32_t kMirrorEvery = 8;

  struct Stripe {
    uint16_t *pixels = nullptr;
    int y0 = 0;
    int rows = 0;
    games::CircuitRenderer::Span span{};
    SemaphoreHandle_t filled = nullptr;  // render task -> main loop
    SemaphoreHandle_t freed = nullptr;   // main loop -> render task
  };

  [[noreturn]] void taskLoop();
  static void taskTrampoline(void *self);
  void renderFrame();
  bool startTask();
#ifdef DREHKLANG_CIRCUIT_DEBUG
  void logTiming(uint32_t renderUs, uint32_t pushUs, uint32_t frameUs);
  uint32_t debugFrames_ = 0;
  uint32_t debugWorstRenderUs_ = 0;
  uint32_t debugWorstPushUs_ = 0;
  uint64_t debugFrameUs_ = 0;
  uint32_t lastRenderUs_ = 0;
#endif

  LvglGlue &glue_;
  games::CircuitRenderer *renderer_ = nullptr;  // PSRAM, while open
  games::CircuitScene scene_;  // written by the main loop between frames
  Stripe stripes_[2];
  TaskHandle_t task_ = nullptr;
  // Set while a frame is being rendered: close() waits it out.
  SemaphoreHandle_t frameDone_ = nullptr;
  uint32_t frames_ = 0;
};

}  // namespace drehklang::ui
