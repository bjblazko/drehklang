// ScreenManager's full-screen visualization (ADR 0034): an analog VU
// meter or an FL bar meter over the music, opened from Now Playing's
// options panel. A tap shows the next one, a swipe goes back, the toggle
// at the bottom keeps the display awake while music plays.
//
// The meters have their own look (ux-guidelines.md, the exception for
// games and visualizations), and so does the toggle: a bare glyph in the
// meter's own colours, unlit when off and lit when on (the user found a
// Theme.h button on the meters disturbing, 2026-11-03).

#include <lvgl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "IconFont.h"
#include "LvglButtonHelpers.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "Theme.h"
#include "VuMeterArt.h"

namespace drehklang::ui {

namespace {

namespace vu = visualizer::vu;
namespace bars = visualizer::bars;

// The keep-awake toggle: bottom centre, on the analog meter's housing.
constexpr lv_coord_t kVizAwakeSize = 44;
constexpr lv_coord_t kVizAwakeCenterY = 330;

constexpr float kSegW = DREHKLANG_VU_BAR_W_X100 / 100.0f;
constexpr float kSegGap = DREHKLANG_VU_BAR_GAP_X100 / 100.0f;
constexpr lv_coord_t kBandH = DREHKLANG_VU_BAR_H + 2 * DREHKLANG_VU_GLOW_MARGIN;
constexpr lv_coord_t kBarLeft = DREHKLANG_VU_BAR_X0 - DREHKLANG_VU_GLOW_MARGIN;
constexpr lv_coord_t kBandY[2] = {DREHKLANG_VU_BAR_L_Y - DREHKLANG_VU_GLOW_MARGIN,
                                  DREHKLANG_VU_BAR_R_Y - DREHKLANG_VU_GLOW_MARGIN};

float segX(int i) { return DREHKLANG_VU_BAR_X0 + static_cast<float>(i) * (kSegW + kSegGap); }

// The toggle's glyph, per meter: off like the meter's unlit parts, on like
// its light -- the lamp-lit cream on the analog housing, the FL legends'
// cyan on the bar meter.
struct ToggleLook {
  lv_color_t off;
  lv_opa_t offOpa;
  lv_color_t on;
};
ToggleLook toggleLook(visualizer::Visualization kind) {
  if (kind == visualizer::Visualization::AnalogVu) {
    return {lv_color_hex(0xF9DF9C), LV_OPA_30, lv_color_hex(0xFFD27A)};
  }
  return {lv_color_hex(0x1A4552), LV_OPA_COVER, lv_color_hex(0x8AE9FF)};
}

// The needle's colours, as in the preview.
lv_color_t needleInk() { return lv_color_hex(0x141210); }
lv_color_t needleShadow() { return lv_color_hex(0x462800); }

lv_obj_t *makePlainImage(lv_obj_t *parent, const lv_img_dsc_t *src, lv_coord_t x, lv_coord_t y) {
  lv_obj_t *img = lv_img_create(parent);
  lv_img_set_src(img, src);
  lv_obj_set_pos(img, x, y);
  lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
  return img;
}

// A box that shows only its part of a lit band: the band image sits at
// the box's negative offset, and the box clips it.
lv_obj_t *makeClip(lv_obj_t *parent, const lv_img_dsc_t *band) {
  lv_obj_t *clip = lv_obj_create(parent);
  lv_obj_remove_style_all(clip);
  lv_obj_clear_flag(clip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(clip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(clip, 0, kBandH);
  makePlainImage(clip, band, 0, 0);
  return clip;
}

void placeClip(lv_obj_t *clip, lv_coord_t x, lv_coord_t y, lv_coord_t width) {
  lv_obj_set_pos(clip, x, y);
  lv_obj_set_width(clip, std::max<lv_coord_t>(0, width));
  lv_obj_set_pos(lv_obj_get_child(clip, 0), static_cast<lv_coord_t>(-x), 0);
}

}  // namespace

void ScreenManager::openVisualization() {
  visualizationOpen_ = true;
  render();
}

bool ScreenManager::closeVisualization() {
  if (!visualizationOpen_) return false;
  visualizationOpen_ = false;
  render();
  return true;
}

void ScreenManager::forgetVisualizationObjects() {
  vizNeedleLayer_ = nullptr;
  vizLampImg_ = nullptr;
  vizLevelClip_[0] = vizLevelClip_[1] = nullptr;
  vizHoldClip_[0] = vizHoldClip_[1] = nullptr;
  vizAwakeButton_ = nullptr;
}

void ScreenManager::renderVisualization() {
  vizShownNeedle_ = -1.0f;
  vizShownLamp_ = -1;
  vizShownLit_[0] = vizShownLit_[1] = -1;
  vizShownHold_[0] = vizShownHold_[1] = -2;

  if (vizSettings_.current() == visualizer::Visualization::AnalogVu) {
    renderAnalogMeter();
  } else {
    renderBarMeter();
  }

  // Over the meter: the whole screen is the place to tap.
  lv_obj_t *tapArea = lv_obj_create(screen_);
  lv_obj_remove_style_all(tapArea);
  lv_obj_set_size(tapArea, drivers::kLcdHorRes, drivers::kLcdVerRes);
  lv_obj_add_flag(tapArea, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(tapArea, onVizPressed, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(tapArea, onVizReleased, LV_EVENT_RELEASED, this);

  // Over the tap area, so a tap on it never also switches the meter.
  vizAwakeButton_ = makeIconButton(screen_, DREHKLANG_ICON_LIGHT_MODE, kVizAwakeSize,
                                   kVizAwakeSize, LV_ALIGN_TOP_MID, 0,
                                   kVizAwakeCenterY - kVizAwakeSize / 2, onVizAwakeClicked, this,
                                   ButtonRole::Quiet, &drehklang_icon_font_28);
  styleVizAwakeButton();
  updateVisualization();
}

void ScreenManager::renderAnalogMeter() {
  makePlainImage(screen_, &drehklang_vu_analog_face, 0, 0);
  // The needle is drawn on this full-screen layer, and only the strip it
  // sweeps is redrawn (updateAnalogMeter()).
  vizNeedleLayer_ = lv_obj_create(screen_);
  lv_obj_remove_style_all(vizNeedleLayer_);
  lv_obj_set_size(vizNeedleLayer_, drivers::kLcdHorRes, drivers::kLcdVerRes);
  lv_obj_clear_flag(vizNeedleLayer_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(vizNeedleLayer_, onVizNeedleDraw, LV_EVENT_DRAW_MAIN, this);
  vizLampImg_ = makePlainImage(screen_, &drehklang_vu_peak_lamp, DREHKLANG_VU_LAMP_X,
                               DREHKLANG_VU_LAMP_Y);
  lv_obj_add_flag(vizLampImg_, LV_OBJ_FLAG_HIDDEN);
  makePlainImage(screen_, &drehklang_vu_analog_glass, DREHKLANG_VU_GLASS_X, DREHKLANG_VU_GLASS_Y);
}

void ScreenManager::renderBarMeter() {
  makePlainImage(screen_, &drehklang_vu_bars_dark, 0, 0);
  const lv_img_dsc_t *bands[2] = {&drehklang_vu_bars_lit_l, &drehklang_vu_bars_lit_r};
  for (int ch = 0; ch < 2; ++ch) {
    vizLevelClip_[ch] = makeClip(screen_, bands[ch]);
    vizHoldClip_[ch] = makeClip(screen_, bands[ch]);
  }
}

void ScreenManager::styleVizAwakeButton() {
  if (!vizAwakeButton_) return;
  const ToggleLook look = toggleLook(vizSettings_.current());
  const bool on = vizSettings_.keepAwake();
  // No pressed shade either: the glyph alone is the control.
  lv_obj_set_style_bg_opa(vizAwakeButton_, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_t *glyph = lv_obj_get_child(vizAwakeButton_, 0);
  lv_obj_set_style_text_color(glyph, on ? look.on : look.off, 0);
  lv_obj_set_style_text_opa(glyph, on ? LV_OPA_COVER : look.offOpa, 0);
}

bool ScreenManager::tickVisualization(uint32_t nowMs, bool visible) {
  if (!visualizationOpen_) return false;
  // Only while seen: holding a dark or locked display awake would wake it.
  const bool playing = playback_.state() == playback::PlaybackState::Playing;
  const bool hold = visible && vizSettings_.holdsDisplayAwake(playing);
  if (!visible) {
    lastVizTickMs_ = nowMs;
    return false;
  }
  uint32_t dtMs = nowMs - lastVizTickMs_;
  if (dtMs < kSpectrumFrameMs) return hold;
  lastVizTickMs_ = nowMs;
  dtMs = std::min<uint32_t>(dtMs, 100);  // After a stall, no jump.

  const signal::StereoLevels levels = vizHold_.apply(playback_.readLevels(), dtMs);
  vizNeedle_.update(vu::needleTarget(levels.rms[0], levels.rms[1]), dtMs);
  vizPeakLamp_.update(levels.peak[0], levels.peak[1], dtMs);
  for (int ch = 0; ch < 2; ++ch) vizBars_[ch].update(levels.peak[ch], dtMs);
  updateVisualization();
  return hold;
}

void ScreenManager::updateVisualization() {
  if (vizNeedleLayer_) updateAnalogMeter();
  if (vizLevelClip_[0]) updateBarMeter();
}

void ScreenManager::updateAnalogMeter() {
  const float position = vizNeedle_.position();
  if (std::fabs(position - vizShownNeedle_) > 0.0005f) {
    auto invalidate = [this](float p) {
      const vu::PixelArea a = vu::needleArea(p);
      lv_area_t area = {a.x1, a.y1, a.x2, a.y2};
      lv_obj_invalidate_area(vizNeedleLayer_, &area);
    };
    if (vizShownNeedle_ >= 0.0f) invalidate(vizShownNeedle_);
    vizShownNeedle_ = position;
    invalidate(position);
  }
  // 16 steps of brightness are smooth enough for a 350 ms fade.
  const int lamp = static_cast<int>(std::lround(vizPeakLamp_.brightness() * 16));
  if (lamp != vizShownLamp_) {
    vizShownLamp_ = lamp;
    if (lamp == 0) {
      lv_obj_add_flag(vizLampImg_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_set_style_img_opa(vizLampImg_, static_cast<lv_opa_t>(std::min(255, lamp * 16)), 0);
      lv_obj_clear_flag(vizLampImg_, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void ScreenManager::updateBarMeter() {
  for (int ch = 0; ch < 2; ++ch) {
    const int lit = vizBars_[ch].litSegments();
    if (lit != vizShownLit_[ch]) {
      vizShownLit_[ch] = lit;
      const lv_coord_t right =
          lit <= 0 ? kBarLeft
                   : static_cast<lv_coord_t>(std::ceil(segX(lit - 1) + kSegW + kSegGap / 2));
      placeClip(vizLevelClip_[ch], kBarLeft, kBandY[ch], static_cast<lv_coord_t>(right - kBarLeft));
    }
    const int hold = vizBars_[ch].holdSegment();
    if (hold != vizShownHold_[ch]) {
      vizShownHold_[ch] = hold;
      if (hold < 0) {
        lv_obj_add_flag(vizHoldClip_[ch], LV_OBJ_FLAG_HIDDEN);
      } else {
        const auto x = static_cast<lv_coord_t>(std::floor(segX(hold) - kSegGap / 2));
        placeClip(vizHoldClip_[ch], x, kBandY[ch],
                  static_cast<lv_coord_t>(std::ceil(kSegW + kSegGap)));
        lv_obj_clear_flag(vizHoldClip_[ch], LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}

// The needle's shadow and then the needle, as tapered polygons.
void ScreenManager::onVizNeedleDraw(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self) return;
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  const auto poly = vu::needlePolygon(self->vizNeedle_.position());
  lv_point_t points[4];
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);

  for (size_t i = 0; i < poly.size(); ++i) {
    points[i] = {static_cast<lv_coord_t>(std::lround(poly[i].x + vu::kShadowDx)),
                 static_cast<lv_coord_t>(std::lround(poly[i].y + vu::kShadowDy))};
  }
  dsc.bg_color = needleShadow();
  dsc.bg_opa = LV_OPA_30;
  lv_draw_polygon(ctx, &dsc, points, 4);

  for (size_t i = 0; i < poly.size(); ++i) {
    points[i] = {static_cast<lv_coord_t>(std::lround(poly[i].x)),
                 static_cast<lv_coord_t>(std::lround(poly[i].y))};
  }
  dsc.bg_color = needleInk();
  dsc.bg_opa = LV_OPA_COVER;
  lv_draw_polygon(ctx, &dsc, points, 4);
}

void ScreenManager::onVizPressed(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self) return;
  lv_indev_get_point(lv_indev_get_act(), &self->vizPressPoint_);
}

// The next meter. Only a tap: a swipe here is the way back.
void ScreenManager::onVizReleased(lv_event_t *e) {
  constexpr lv_coord_t kTapSlop = 10;
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self) return;
  lv_point_t point;
  lv_indev_get_point(lv_indev_get_act(), &point);
  if (std::abs(point.x - self->vizPressPoint_.x) > kTapSlop ||
      std::abs(point.y - self->vizPressPoint_.y) > kTapSlop) {
    return;
  }
  self->vizSettings_.next();
  self->render();
}

void ScreenManager::onVizAwakeClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self) return;
  self->vizSettings_.toggleKeepAwake();
  self->styleVizAwakeButton();
}

void ScreenManager::onVisualsClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->optionsPanelOpen_ = false;
  self->openVisualization();
}

}  // namespace drehklang::ui
