// ScreenManager's Circuit screen (ADR 0030) -- one file per game, beside
// ScreenManagerGravity.cpp.
//
// Unlike the other games nothing here is drawn by LVGL: every row of a
// pseudo-3D road changes every frame, so CircuitPresenter renders whole
// stripes and owns the panel while the game is on it. LVGL keeps one
// invisible object, the brake, which covers the screen.

#include <lvgl.h>

#include "CircuitPresenter.h"
#include "CircuitScene.h"
#include "CircuitSounds.h"
#include "CircuitTracks.h"
#include "GameScreenStyle.h"
#include "ScreenManager.h"
#include "St77916Driver.h"

namespace drehklang::ui {

using games::CircuitGame;
using Phase = games::CircuitGame::Phase;

namespace {

// "EXTENDED TIME" shows this long after a checkpoint or a lap.
constexpr uint32_t kBonusShownMs = 1600;

}  // namespace

void ScreenManager::renderCircuit() {
  // Black under the game, for the moment before the first frame.
  lv_obj_set_style_bg_color(screen_, game_style::vacuum(), 0);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);

  // The whole screen is the brake -- the throttle is always open once the
  // lights go (user, 2026-10-06). PRESS_LOST as well as RELEASED: a finger
  // that slides off the edge must not leave the brake on.
  circuitBrake_ = lv_obj_create(screen_);
  lv_obj_set_size(circuitBrake_, drivers::kLcdHorRes, drivers::kLcdVerRes);
  lv_obj_set_pos(circuitBrake_, 0, 0);
  lv_obj_set_style_bg_opa(circuitBrake_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(circuitBrake_, 0, 0);
  lv_obj_clear_flag(circuitBrake_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(circuitBrake_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(circuitBrake_, onCircuitPressed, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(circuitBrake_, onCircuitReleased, LV_EVENT_RELEASED, this);
  lv_obj_add_event_cb(circuitBrake_, onCircuitReleased, LV_EVENT_PRESS_LOST, this);
  lv_obj_add_event_cb(circuitBrake_, onCircuitTapped, LV_EVENT_CLICKED, this);

  circuitRecords_.load(settings_);
  circuit_.select(games::kTracks[circuitTrack_]);
  shownCircuitPhase_ = Phase::Select;
  circuitHeld_ = games::circuit_sound::Held{};
  circuitNewRecord_ = false;
  circuitBonusSeen_ = false;
  lastCircuitDrawMs_ = 0;
}

// Leaving, for whatever reason: the panel goes back to LVGL and the
// engine stops.
void ScreenManager::leaveCircuit() {
  if (circuitPresenter_) circuitPresenter_->close();
  if (blips_) blips_->silence();
  circuitBrake_ = nullptr;
  circuitHeld_ = games::circuit_sound::Held{};
}

void ScreenManager::onCircuitPressed(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (self && self->circuitBrake_) self->circuit_.setBraking(true);
}

void ScreenManager::onCircuitReleased(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (self && self->circuitBrake_) self->circuit_.setBraking(false);
}

// A tap starts the countdown from track select and returns to it from the
// result. Mid-race a tap is just a short brake.
void ScreenManager::onCircuitTapped(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->circuitBrake_) return;
  CircuitGame &game = self->circuit_;
  if (game.phase() == Phase::Select) {
    game.setBraking(false);
    game.startCountdown(lv_tick_get());
  } else if (game.phase() == Phase::Over) {
    game.select(games::kTracks[self->circuitTrack_]);
    self->circuitNewRecord_ = false;
  }
}

// On track select the knob chooses the track; from the countdown on it is
// the wheel. Clockwise steers right.
void ScreenManager::onCircuitKnob(int16_t delta) {
  if (!circuitBrake_) return;
  if (circuit_.phase() == Phase::Select) {
    const int step = delta > 0 ? 1 : -1;
    circuitTrack_ = (circuitTrack_ + step + games::kTrackCount) % games::kTrackCount;
    circuit_.select(games::kTracks[circuitTrack_]);
    lastCircuitDrawMs_ = 0;  // Show the new track at once.
    return;
  }
  circuit_.steer(delta);
}

bool ScreenManager::tickCircuit(uint32_t nowMs, bool visible) {
  if (!circuitBrake_ || !circuitPresenter_) return false;
  if (tabs_.activeStack().current().kind != navigation::ScreenKind::Circuit) return false;
  // Locked or dark: the lock screen is LVGL's to draw, and nothing needs
  // the game's memory meanwhile. Reopened on the way back.
  if (!visible) {
    circuitPresenter_->close();
    return false;
  }
  if (!circuitPresenter_->isOpen()) {
    if (!circuitPresenter_->open()) return false;
    lastCircuitDrawMs_ = 0;
  }

  // Physics and sound every loop, the picture on its own gate (ADR 0022).
  circuit_.tick(nowMs);
  drainCircuitSounds(nowMs);
  if (circuit_.phase() == Phase::Over && shownCircuitPhase_ != Phase::Over) finishCircuitRace();
  shownCircuitPhase_ = circuit_.phase();

  if (lastCircuitDrawMs_ == 0 || nowMs - lastCircuitDrawMs_ >= kCircuitFrameMs) {
    lastCircuitDrawMs_ = nowMs;
    presentCircuit(nowMs);
  }
  return circuit_.phase() == Phase::Countdown || circuit_.phase() == Phase::Race;
}

// The race's best lap against the track's record; NVS is written only
// when a record falls.
void ScreenManager::finishCircuitRace() {
  circuitNewRecord_ = circuitRecords_.offer(circuitTrack_, circuit_.bestLapMs());
  if (circuitNewRecord_) circuitRecords_.save(settings_, circuitTrack_);
}

void ScreenManager::presentCircuit(uint32_t nowMs) {
  games::CircuitScene scene = games::CircuitScene::capture(circuit_);
  scene.trackIndex = circuitTrack_;
  scene.recordMs = circuitRecords_.best(circuitTrack_);
  scene.newRecord = circuitNewRecord_;
  scene.frame = ++circuitFrame_;
  scene.sinceBonusMs = circuitBonusSeen_ ? nowMs - circuitBonusAtMs_ : UINT32_MAX;
  if (scene.sinceBonusMs > kBonusShownMs) circuitBonusSeen_ = false;
  circuitPresenter_->present(scene);
}

// The engine and the tyres are states, set whenever they change; the
// rest are one-shots from the game's queue (ADR 0023's lesson).
void ScreenManager::drainCircuitSounds(uint32_t nowMs) {
  for (CircuitGame::Sound sound = circuit_.takeSound(); sound != CircuitGame::Sound::None;
       sound = circuit_.takeSound()) {
    if (sound == CircuitGame::Sound::Checkpoint || sound == CircuitGame::Sound::Lap) {
      circuitBonusAtMs_ = nowMs;
      circuitBonusSeen_ = true;
    }
    if (blips_) blips_->chipEffect(games::circuit_sound::effectFor(sound));
  }
  if (!blips_) return;
  const games::circuit_sound::Held held = games::circuit_sound::heldFor(circuit_);
  if (held == circuitHeld_) return;
  if (held.engineHz != circuitHeld_.engineHz || held.engineLevel != circuitHeld_.engineLevel) {
    blips_->chipEngine(held.engineHz, held.engineLevel);
  }
  if (held.noiseHz != circuitHeld_.noiseHz || held.noiseLevel != circuitHeld_.noiseLevel) {
    blips_->chipNoise(held.noiseHz, held.noiseLevel);
  }
  circuitHeld_ = held;
}

}  // namespace drehklang::ui
