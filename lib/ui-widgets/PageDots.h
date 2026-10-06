#pragma once

#include <lvgl.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace drehklang::ui_widgets {

// A row of page dots under something that is swiped through: the current
// page's dot in `current`, the others in `other`. Centred on the screen's
// vertical axis at `y` (a round screen has no usable corners).
class PageDots {
 public:
  static constexpr size_t kMaxPages = 4;

  void create(lv_obj_t *parent, size_t count, lv_coord_t y, lv_color_t current,
              lv_color_t other) {
    count_ = std::min(count, kMaxPages);
    current_ = current;
    other_ = other;
    for (size_t i = 0; i < count_; ++i) {
      lv_obj_t *dot = lv_obj_create(parent);
      lv_obj_remove_style_all(dot);
      lv_obj_set_size(dot, kSize, kSize);
      const auto offset = static_cast<lv_coord_t>(
          (2 * static_cast<int>(i) - static_cast<int>(count_) + 1) * kPitch / 2);
      lv_obj_align(dot, LV_ALIGN_TOP_MID, offset, y);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
      lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
      dots_[i] = dot;
    }
    setCurrent(0);
  }

  void setCurrent(size_t page) {
    for (size_t i = 0; i < count_; ++i) {
      if (dots_[i]) lv_obj_set_style_bg_color(dots_[i], i == page ? current_ : other_, 0);
    }
  }

  // The screen that owned the objects is being deleted.
  void detach() {
    dots_.fill(nullptr);
    count_ = 0;
  }

 private:
  static constexpr lv_coord_t kSize = 6;
  // Dot plus gap, as the tone generator spaced its two (ADR 0024).
  static constexpr lv_coord_t kPitch = 14;

  std::array<lv_obj_t *, kMaxPages> dots_{};
  size_t count_ = 0;
  lv_color_t current_{};
  lv_color_t other_{};
};

}  // namespace drehklang::ui_widgets
