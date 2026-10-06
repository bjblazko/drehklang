// ScreenManager's equalizer screen (ADR 0029), beside the other
// one-file-per-screen parts of ScreenManager.
//
// Top to bottom, all centred: back and caption, the selected band and its
// gain, seven sliders on a 0 dB centre line, their frequencies, and Flat.
// Tapping a slider selects it and the knob sets it in 1 dB steps; dragging
// one sets it directly. Only the selected handle is in accent: the value
// being set, as on Brightness.

#include <lvgl.h>

#include <cstdio>
#include <cstdlib>

#include "LvglButtonHelpers.h"
#include "ScreenManager.h"
#include "TextFont.h"
#include "Theme.h"

namespace drehklang::ui {

using signal::GraphicEqualizer;

namespace {
constexpr lv_coord_t kValueY = 76;
constexpr lv_coord_t kColumnWidth = 36;
constexpr lv_coord_t kPxPerDb = 6;
constexpr lv_coord_t kTravel = 2 * GraphicEqualizer::kMaxGainDb * kPxPerDb;  // 144.
// The area takes a few pixels above and below the travel, so a handle at
// either end is still easy to reach.
constexpr lv_coord_t kAreaPad = 8;
constexpr lv_coord_t kAreaY = 112 - kAreaPad;
constexpr lv_coord_t kAreaHeight = kTravel + 2 * kAreaPad;
constexpr lv_coord_t kCentreY = kAreaPad + kTravel / 2;  // Within the area.
constexpr lv_coord_t kHandleWidth = 20;
constexpr lv_coord_t kHandleHeight = 8;
constexpr lv_coord_t kLabelY = kAreaY + kAreaHeight + 2;
constexpr lv_coord_t kFlatY = 290;
// A press that moves this far is a drag; less is a tap that only selects.
constexpr lv_coord_t kDragStartPx = 6;

const char *const kBandLabels[GraphicEqualizer::kBands] = {"31",  "100", "300", "1k",
                                                          "3k", "8k",  "16k"};

lv_obj_t *makeLine(lv_obj_t *parent, lv_point_t *points, lv_coord_t width, lv_color_t color) {
  lv_obj_t *line = lv_line_create(parent);
  lv_line_set_points(line, points, 2);
  lv_obj_set_style_line_width(line, width, 0);
  lv_obj_set_style_line_color(line, color, 0);
  lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
  return line;
}

// "100 Hz · +4 dB", "16 kHz · 0 dB".
void formatBand(char *out, size_t size, size_t band, int db) {
  const float hz = GraphicEqualizer::kFrequenciesHz[band];
  char freq[12];
  if (hz >= 1000.0f) {
    snprintf(freq, sizeof(freq), "%d kHz", static_cast<int>(hz / 1000.0f));
  } else {
    snprintf(freq, sizeof(freq), "%d Hz", static_cast<int>(hz));
  }
  snprintf(out, size, db > 0 ? "%s \xC2\xB7 +%d dB" : "%s \xC2\xB7 %d dB", freq, db);
}
}  // namespace

void ScreenManager::renderEqualizer() {
  if (!equalizer_) return;
  eqValueLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(eqValueLabel_, &drehklang_text_font_20, 0);
  lv_obj_set_style_text_color(eqValueLabel_, theme::ink(), 0);
  lv_obj_align(eqValueLabel_, LV_ALIGN_TOP_MID, 0, kValueY);

  const lv_coord_t areaWidth = kColumnWidth * GraphicEqualizer::kBands;
  eqArea_ = lv_obj_create(screen_);
  lv_obj_remove_style_all(eqArea_);
  lv_obj_set_size(eqArea_, areaWidth, kAreaHeight);
  lv_obj_align(eqArea_, LV_ALIGN_TOP_MID, 0, kAreaY);
  lv_obj_clear_flag(eqArea_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(eqArea_, LV_OBJ_FLAG_CLICKABLE);

  // The 0 dB line across all bands, under the tracks.
  eqCentreLine_ = {{{0, kCentreY}, {static_cast<lv_coord_t>(areaWidth - 1), kCentreY}}};
  makeLine(eqArea_, eqCentreLine_.data(), 1, theme::structure());

  for (size_t b = 0; b < GraphicEqualizer::kBands; ++b) {
    const auto x = static_cast<lv_coord_t>(b * kColumnWidth + kColumnWidth / 2);
    eqTracks_[b] = {{{x, kAreaPad}, {x, static_cast<lv_coord_t>(kAreaPad + kTravel)}}};
    makeLine(eqArea_, eqTracks_[b].data(), 2, theme::surfaceAlt());

    eqHandles_[b] = lv_obj_create(eqArea_);
    lv_obj_remove_style_all(eqHandles_[b]);
    lv_obj_set_size(eqHandles_[b], kHandleWidth, kHandleHeight);
    lv_obj_set_style_radius(eqHandles_[b], 2, 0);
    lv_obj_set_style_bg_opa(eqHandles_[b], LV_OPA_COVER, 0);
    lv_obj_clear_flag(eqHandles_[b], LV_OBJ_FLAG_CLICKABLE);

    // The whole column takes the finger, not just the thin track.
    eqColumnContexts_[b] = EqColumnContext{this, b};
    lv_obj_t *column = lv_obj_create(eqArea_);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, kColumnWidth, kAreaHeight);
    lv_obj_set_pos(column, static_cast<lv_coord_t>(b * kColumnWidth), 0);
    lv_obj_add_flag(column, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(column, onEqColumnPressed, LV_EVENT_PRESSED, &eqColumnContexts_[b]);
    lv_obj_add_event_cb(column, onEqColumnPressing, LV_EVENT_PRESSING, &eqColumnContexts_[b]);

    lv_obj_t *label = lv_label_create(screen_);
    lv_obj_set_style_text_font(label, &drehklang_text_font_14, 0);
    lv_obj_set_style_text_color(label, theme::structure(), 0);
    lv_label_set_text(label, kBandLabels[b]);
    lv_obj_align(label, LV_ALIGN_TOP_MID,
                 static_cast<lv_coord_t>((static_cast<int>(b) - 3) * kColumnWidth), kLabelY);
  }

  makeIconButton(screen_, "Flat", 88, 36, LV_ALIGN_TOP_MID, 0, kFlatY, onEqFlatClicked, this,
                 ButtonRole::Secondary, &drehklang_text_font_16);
  updateEqualizerDisplay();
}

void ScreenManager::updateEqualizerDisplay() {
  if (!equalizer_ || !eqValueLabel_) return;
  for (size_t b = 0; b < GraphicEqualizer::kBands; ++b) {
    if (!eqHandles_[b]) continue;
    const bool selected = b == equalizer_->selected();
    const lv_coord_t y = kCentreY - equalizer_->gain(b) * kPxPerDb - kHandleHeight / 2;
    lv_obj_set_pos(eqHandles_[b],
                   static_cast<lv_coord_t>(b * kColumnWidth + (kColumnWidth - kHandleWidth) / 2),
                   y);
    lv_obj_set_style_bg_color(eqHandles_[b], selected ? theme::accent() : theme::ink(), 0);
  }
  char text[32];
  formatBand(text, sizeof(text), equalizer_->selected(),
             equalizer_->gain(equalizer_->selected()));
  lv_label_set_text(eqValueLabel_, text);
}

void ScreenManager::onEqColumnPressed(lv_event_t *e) {
  auto *context = static_cast<EqColumnContext *>(lv_event_get_user_data(e));
  if (!context || !context->self || !context->self->equalizer_) return;
  lv_point_t point;
  lv_indev_get_point(lv_indev_get_act(), &point);
  context->self->eqPressY_ = point.y;
  context->self->eqDragging_ = false;
  context->self->equalizer_->select(context->band);
  context->self->updateEqualizerDisplay();
}

// A drag sets the band to where the finger is; a press that has not moved
// yet only selected it.
void ScreenManager::onEqColumnPressing(lv_event_t *e) {
  auto *context = static_cast<EqColumnContext *>(lv_event_get_user_data(e));
  if (!context || !context->self || !context->self->equalizer_) return;
  ScreenManager &self = *context->self;
  lv_point_t point;
  lv_indev_get_point(lv_indev_get_act(), &point);
  if (!self.eqDragging_ && std::abs(point.y - self.eqPressY_) < kDragStartPx) return;
  self.eqDragging_ = true;
  lv_area_t area;
  lv_obj_get_coords(self.eqArea_, &area);
  const lv_coord_t fromCentre = area.y1 + kCentreY - point.y;
  const int db = (fromCentre + (fromCentre >= 0 ? kPxPerDb / 2 : -kPxPerDb / 2)) / kPxPerDb;
  if (db == self.equalizer_->gain(context->band)) return;
  self.equalizer_->set(context->band, db, lv_tick_get());
  self.updateEqualizerDisplay();
}

void ScreenManager::onEqFlatClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->equalizer_) return;
  self->equalizer_->makeFlat(lv_tick_get());
  self->updateEqualizerDisplay();
}

}  // namespace drehklang::ui
