// ScreenManager's tone generator screen (ADR 0024), beside the other
// one-file-per-screen parts of ScreenManager.
//
// Top to bottom, all centred -- a round screen has no usable corners
// (ux-guidelines §7): back and caption, the band (scope or spectrum, swiped
// between), what the band's scales are, which page it is on, the selected
// value, the chips, and Play/Stop as the one primary button.

#include <lvgl.h>

#include <algorithm>
#include <cstring>

#include "GeneratorControl.h"
#include "LvglButtonHelpers.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "TextFont.h"
#include "Theme.h"
#include "ToneSettings.h"

#ifdef DREHKLANG_GENERATOR_DEBUG
#include <Arduino.h>
#include <esp_timer.h>
#endif

namespace drehklang::ui {

using signal::ToneParam;
using signal::ToneSettings;
using signal::Waveform;

namespace {

constexpr lv_coord_t kBandWidth = 260;
constexpr lv_coord_t kBandHeight = 88;
constexpr lv_coord_t kBandY = 76;
constexpr lv_coord_t kScaleLabelY = kBandY + kBandHeight + 4;
constexpr lv_coord_t kDotsY = kScaleLabelY + 22;
constexpr lv_coord_t kValueY = 198;
constexpr lv_coord_t kChipY = 238;
constexpr lv_coord_t kChipWidth = 68;
constexpr lv_coord_t kChipHeight = 36;
constexpr lv_coord_t kChipGap = 6;
constexpr lv_coord_t kPlaySize = 56;
constexpr lv_coord_t kPlayY = 286;
// How far a finger must travel across the band to turn its page.
constexpr lv_coord_t kSwipeMinPx = 40;

// Noise has no pitch to lock on to; this picks the 20 ms timebase.
constexpr float kNoiseTimebaseHz = 100.0f;

constexpr ToneParam kToneChipOrder[signal::kToneParamCount] = {
    ToneParam::Waveform, ToneParam::Frequency, ToneParam::Level, ToneParam::Shape};

// The frequency the scope's timebase is chosen for.
float timebaseHz(const signal::OscillatorParams &params) {
  return params.waveform == Waveform::Noise ? kNoiseTimebaseHz : params.frequencyHz;
}

}  // namespace

void ScreenManager::renderToneGenerator() {
  if (!toneSession_) return;

  // The band's box takes the finger: swiping it turns the band's page.
  lv_obj_t *band = toneBand_.create(screen_, kBandWidth, kBandHeight, theme::ink(),
                                    theme::surfaceAlt());
  lv_obj_align(band, LV_ALIGN_TOP_MID, 0, kBandY);
  lv_obj_add_flag(band, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(band, onToneBandPressed, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(band, onToneBandReleased, LV_EVENT_RELEASED, this);
  lv_obj_add_event_cb(band, onToneBandReleased, LV_EVENT_PRESS_LOST, this);

  toneScaleLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(toneScaleLabel_, &drehklang_text_font_14, 0);
  lv_obj_set_style_text_color(toneScaleLabel_, theme::structure(), 0);
  lv_obj_align(toneScaleLabel_, LV_ALIGN_TOP_MID, 0, kScaleLabelY);

  toneDots_.create(screen_, 2, kDotsY, theme::ink(), theme::surfaceAlt());

  toneValueLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(toneValueLabel_, &drehklang_text_font_28, 0);
  lv_obj_set_style_text_color(toneValueLabel_, theme::ink(), 0);
  lv_obj_align(toneValueLabel_, LV_ALIGN_TOP_MID, 0, kValueY);

  for (int i = 0; i < signal::kToneParamCount; ++i) {
    toneChipContexts_[i] = ToneChipContext{this, kToneChipOrder[i]};
    lv_obj_t *chip = lv_btn_create(screen_);
    lv_obj_set_size(chip, kChipWidth, kChipHeight);
    lv_obj_set_ext_click_area(chip, kChipGap / 2);
    theme::styleSecondaryButton(chip);
    lv_obj_t *label = lv_label_create(chip);
    lv_obj_set_style_text_font(label, &drehklang_text_font_16, 0);
    lv_obj_center(label);
    // On the press, not the click: holding a chip and turning the knob
    // with the other hand is this device's two-handed gesture (§7).
    lv_obj_add_event_cb(chip, onToneChipPressed, LV_EVENT_PRESSED,
                        &toneChipContexts_[i]);
    toneChips_[i] = chip;
  }

  tonePlayButton_ = makeIconButton(screen_, LV_SYMBOL_PLAY, kPlaySize, kPlaySize,
                                   LV_ALIGN_TOP_MID, 0, kPlayY, onTonePlayClicked,
                                   this, ButtonRole::Primary);

  lastToneScopeMs_ = lv_tick_get();
  applyTonePlayButton();
  applyToneView();
  updateToneGeneratorDisplay();
}

void ScreenManager::updateToneGeneratorDisplay() {
  if (!toneSession_ || !toneValueLabel_) return;
  const ToneSettings &settings = toneSession_->settings();
  char text[24];
  settings.valueText(settings.selected(), text, sizeof(text));
  lv_label_set_text(toneValueLabel_, text);
  layoutToneChips();
  applyToneLabel();
}

// Shows the chips this waveform has, centred as a row, the selected one
// filled in ink like a selected list row.
void ScreenManager::layoutToneChips() {
  const ToneSettings &settings = toneSession_->settings();
  const Waveform wave = settings.waveform();
  int shown = 0;
  for (ToneParam param : kToneChipOrder) shown += ToneSettings::visible(param, wave);
  const lv_coord_t rowWidth = shown * kChipWidth + (shown - 1) * kChipGap;
  lv_coord_t x = (drivers::kLcdHorRes - rowWidth) / 2;

  for (int i = 0; i < signal::kToneParamCount; ++i) {
    lv_obj_t *chip = toneChips_[i];
    if (!chip) continue;
    const ToneParam param = kToneChipOrder[i];
    if (!ToneSettings::visible(param, wave)) {
      lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(chip, x, kChipY);
    x += kChipWidth + kChipGap;
    const bool selected = settings.selected() == param;
    lv_obj_set_style_bg_color(chip, selected ? theme::ink() : theme::surfaceAlt(), 0);
    lv_obj_t *label = lv_obj_get_child(chip, 0);
    lv_obj_set_style_text_color(label, selected ? theme::surface() : theme::ink(), 0);
    lv_label_set_text(label, ToneSettings::chipLabel(param, wave));
  }
}

void ScreenManager::applyTonePlayButton() {
  if (!tonePlayButton_ || !toneSession_) return;
  shownToneRunning_ = toneSession_->running();
  lv_label_set_text(lv_obj_get_child(tonePlayButton_, 0),
                    shownToneRunning_ ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
}

void ScreenManager::applyToneView() {
  toneBand_.setView(toneBand_.view());
  toneDots_.setCurrent(toneBand_.view() == SignalBand::View::Scope ? 0 : 1);
  applyToneLabel();
}

// The scope says what its scales are, the way a bench scope's time/div
// and volts/div do; the spectrum says what its axis spans.
void ScreenManager::applyToneLabel() {
  if (!toneScaleLabel_ || !toneSession_) return;
  char text[40];
  const ToneSettings &settings = toneSession_->settings();
  toneBand_.updateScale(timebaseHz(settings.params()), settings.levelDb());
  toneBand_.label(text, sizeof(text));
  if (strcmp(lv_label_get_text(toneScaleLabel_), text) != 0) {
    lv_label_set_text(toneScaleLabel_, text);
  }
}

void ScreenManager::onToneBandPressed(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  lv_point_t point;
  lv_indev_get_point(lv_indev_get_act(), &point);
  if (self) self->toneSwipeStartX_ = point.x;
}

// Right to left brings the spectrum in, left to right takes it back out,
// like turning a page; a tap does nothing.
void ScreenManager::onToneBandReleased(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self) return;
  lv_point_t point;
  lv_indev_get_point(lv_indev_get_act(), &point);
  const lv_coord_t dx = point.x - self->toneSwipeStartX_;
  SignalBand::View next = self->toneBand_.view();
  if (dx <= -kSwipeMinPx) next = SignalBand::View::Spectrum;
  if (dx >= kSwipeMinPx) next = SignalBand::View::Scope;
  if (next == self->toneBand_.view()) return;
  self->toneBand_.setView(next);
  self->applyToneView();
}

void ScreenManager::onToneChipPressed(lv_event_t *e) {
  auto *context = static_cast<ToneChipContext *>(lv_event_get_user_data(e));
  if (!context || !context->self || !context->self->toneSession_) return;
  context->self->toneSession_->select(context->param);
  context->self->updateToneGeneratorDisplay();
}

void ScreenManager::onTonePlayClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->toneSession_) return;
  signal::ToneSession &session = *self->toneSession_;
  if (session.running()) {
    session.stop();
  } else {
    // A tone is for measuring, so it plays alone (ADR 0024). Paused, not
    // stopped: the track is still there to resume afterwards.
    if (self->playback_.state() == playback::PlaybackState::Playing) {
      self->playback_.togglePlayPause(lv_tick_get());
    }
    session.start();
  }
  self->applyTonePlayButton();
}

void ScreenManager::tickToneGenerator(uint32_t nowMs, bool visible) {
  if (!toneSession_ || !toneValueLabel_) return;
  if (shownToneRunning_ != toneSession_->running()) applyTonePlayButton();
  if (!visible) {
    lastToneScopeMs_ = nowMs;
    return;
  }
  const uint32_t dtMs = nowMs - lastToneScopeMs_;
  if (dtMs < kToneScopeFrameMs) return;
  lastToneScopeMs_ = nowMs;

#ifdef DREHKLANG_GENERATOR_DEBUG
  const int64_t startUs = esp_timer_get_time();
#endif
  const size_t count =
      scopeSource_ ? scopeSource_->readRecent(toneBand_.buffer(), toneBand_.capacity()) : 0;
  if (toneBand_.view() == SignalBand::View::Scope) {
    drawToneScope(count);
  } else {
    drawToneSpectrum(count, std::min<uint32_t>(dtMs, 100));
  }
#ifdef DREHKLANG_GENERATOR_DEBUG
  static uint32_t frames = 0;
  static int64_t worstUs = 0;
  worstUs = std::max(worstUs, esp_timer_get_time() - startUs);
  if (++frames % 90 == 0) {
    // Dropped rather than blocking when the port is not being drained
    // (AGENTS.md: USBCDC::write() has no timeout).
    char line[48];
    const int length = snprintf(line, sizeof(line), "[tones] %s frame worst %lldus\n",
                                toneBand_.view() == SignalBand::View::Scope ? "scope"
                                                                            : "spectrum",
                                worstUs);
    if (length > 0 && Serial.availableForWrite() >= length) {
      Serial.write(reinterpret_cast<const uint8_t *>(line), length);
    }
    worstUs = 0;
  }
#endif
}

void ScreenManager::drawToneScope(size_t count) {
  if (count == 0) {
    // Nothing new reached the DAC: stopped (or never started).
    if (!toneSession_->running()) toneBand_.clearScope();
    return;
  }
  const ToneSettings &settings = toneSession_->settings();
  toneBand_.showScope(count, signal::kGeneratorSampleRate, timebaseHz(settings.params()),
                      settings.levelDb());
  applyToneLabel();
}

void ScreenManager::drawToneSpectrum(size_t count, uint32_t dtMs) {
  toneBand_.showSpectrum(count, signal::kGeneratorSampleRate, dtMs);
}

}  // namespace drehklang::ui
