#include "CircuitPresenter.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <new>

namespace drehklang::ui {

namespace {

constexpr uint32_t kTaskStackBytes = 4096;
// Below the tone task (2) and the decoder task (3) on the same core: a
// late frame is a dropped frame, a late sound is a click.
constexpr UBaseType_t kTaskPriority = 1;
constexpr BaseType_t kRenderCore = 0;
constexpr size_t kStripeBytes =
    games::CircuitRenderer::kSize * 16 * sizeof(uint16_t);

// Internal DMA RAM pushes ~7 % faster than PSRAM (measured 2026-10-06),
// but a PSRAM stripe still works when the internal heap is short.
uint16_t *allocateStripe() {
  void *pixels = heap_caps_malloc(kStripeBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!pixels) pixels = heap_caps_malloc(kStripeBytes, MALLOC_CAP_SPIRAM);
  return static_cast<uint16_t *>(pixels);
}

}  // namespace

bool CircuitPresenter::startTask() {
  if (task_) return true;
  for (Stripe &stripe : stripes_) {
    stripe.filled = xSemaphoreCreateBinary();
    stripe.freed = xSemaphoreCreateBinary();
  }
  frameDone_ = xSemaphoreCreateBinary();
  return xTaskCreatePinnedToCore(&CircuitPresenter::taskTrampoline, "circuit",
                                 kTaskStackBytes, this, kTaskPriority, &task_,
                                 kRenderCore) == pdPASS;
}

bool CircuitPresenter::open() {
  if (renderer_) return true;
  if (!startTask()) return false;
  void *memory = heap_caps_malloc(sizeof(games::CircuitRenderer), MALLOC_CAP_SPIRAM);
  if (!memory) return false;
  renderer_ = new (memory) games::CircuitRenderer();
  for (Stripe &stripe : stripes_) {
    stripe.pixels = allocateStripe();
    // Empty both handovers, then mark the buffer free for the task.
    xSemaphoreTake(stripe.filled, 0);
    xSemaphoreTake(stripe.freed, 0);
    xSemaphoreGive(stripe.freed);
  }
  xSemaphoreTake(frameDone_, 0);
  if (!stripes_[0].pixels || !stripes_[1].pixels) {
    close();
    return false;
  }
  glue_.setPanelOwnedByGame(true);
  return true;
}

void CircuitPresenter::close() {
  if (!renderer_) return;
  glue_.setPanelOwnedByGame(false);
  // present() waits for every frame it starts, so the task is idle here.
  for (Stripe &stripe : stripes_) {
    heap_caps_free(stripe.pixels);
    stripe.pixels = nullptr;
  }
  renderer_->~CircuitRenderer();
  heap_caps_free(renderer_);
  renderer_ = nullptr;
}

void CircuitPresenter::present(const games::CircuitScene &scene) {
  if (!renderer_) return;
  const int64_t start = esp_timer_get_time();
  scene_ = scene;
  xTaskNotifyGive(task_);
  int64_t pushUs = 0;
  const bool mirror = ++frames_ % kMirrorEvery == 0;
  for (int k = 0; k < kStripes; ++k) {
    Stripe &stripe = stripes_[k & 1];
    if (xSemaphoreTake(stripe.filled, pdMS_TO_TICKS(kStripeTimeoutMs)) != pdTRUE) break;
    const int64_t before = esp_timer_get_time();
    glue_.pushStripe(stripe.span.x0, stripe.y0, stripe.span.width, stripe.rows,
                     stripe.pixels, mirror);
    pushUs += esp_timer_get_time() - before;
    xSemaphoreGive(stripe.freed);
  }
  // Never leave with the task still holding a buffer.
  xSemaphoreTake(frameDone_, pdMS_TO_TICKS(kStripeTimeoutMs * 2));
#ifdef DREHKLANG_CIRCUIT_DEBUG
  logTiming(lastRenderUs_, static_cast<uint32_t>(pushUs),
            static_cast<uint32_t>(esp_timer_get_time() - start));
#else
  (void)start;
  (void)pushUs;
#endif
}

void CircuitPresenter::taskTrampoline(void *self) {
  static_cast<CircuitPresenter *>(self)->taskLoop();
}

void CircuitPresenter::taskLoop() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    renderFrame();
    xSemaphoreGive(frameDone_);
  }
}

void CircuitPresenter::renderFrame() {
#ifdef DREHKLANG_CIRCUIT_DEBUG
  int64_t busyUs = 0;
  int64_t mark = esp_timer_get_time();
#endif
  renderer_->prepare(scene_);
#ifdef DREHKLANG_CIRCUIT_DEBUG
  busyUs += esp_timer_get_time() - mark;
#endif
  for (int k = 0; k < kStripes; ++k) {
    Stripe &stripe = stripes_[k & 1];
    xSemaphoreTake(stripe.freed, portMAX_DELAY);
#ifdef DREHKLANG_CIRCUIT_DEBUG
    mark = esp_timer_get_time();
#endif
    stripe.y0 = k * kStripeRows;
    stripe.rows = games::CircuitRenderer::kSize - stripe.y0 < kStripeRows
                      ? games::CircuitRenderer::kSize - stripe.y0
                      : kStripeRows;
    stripe.span = games::CircuitRenderer::stripeSpan(stripe.y0, stripe.rows);
    renderer_->renderStripe(stripe.pixels, stripe.y0, stripe.rows, stripe.span);
#ifdef DREHKLANG_CIRCUIT_DEBUG
    busyUs += esp_timer_get_time() - mark;
#endif
    xSemaphoreGive(stripe.filled);
  }
#ifdef DREHKLANG_CIRCUIT_DEBUG
  lastRenderUs_ = static_cast<uint32_t>(busyUs);
#endif
}

#ifdef DREHKLANG_CIRCUIT_DEBUG
// From the main loop (core 1) only: never print from the render task
// (AGENTS.md, core 0 and USBCDC).
void CircuitPresenter::logTiming(uint32_t renderUs, uint32_t pushUs, uint32_t frameUs) {
  if (renderUs > debugWorstRenderUs_) debugWorstRenderUs_ = renderUs;
  if (pushUs > debugWorstPushUs_) debugWorstPushUs_ = pushUs;
  debugFrameUs_ += frameUs;
  if (++debugFrames_ < 90) return;
  Serial.printf("[circuit] frame %lu us avg, worst render %lu us, worst push %lu us, "
                "internal free %u\n",
                static_cast<unsigned long>(debugFrameUs_ / debugFrames_),
                static_cast<unsigned long>(debugWorstRenderUs_),
                static_cast<unsigned long>(debugWorstPushUs_),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  debugFrames_ = 0;
  debugWorstRenderUs_ = debugWorstPushUs_ = 0;
  debugFrameUs_ = 0;
}
#endif

}  // namespace drehklang::ui
