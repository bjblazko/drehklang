#pragma once

#include <lvgl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "ScopeScale.h"
#include "ScopeTrace.h"
#include "Spectrum.h"
#include "ToneSettings.h"
#include "TriggeredScope.h"

namespace drehklang::ui {

// The band the tone generator draws its scope or spectrum in (ADR 0024),
// as one piece that any screen can show a sample stream in -- Now Playing
// shows the music in it too (ADR 0028). It knows nothing about where the
// samples come from: the caller reads them into buffer() and says at what
// rate.
//
// Scope: a triggered, reconstructed trace on stepped scales (ScopeScale);
// the caller says which frequency to choose the timebase for and which
// level the range is for. Spectrum: 20 Hz - 20 kHz on a log axis, 0 dBFS
// at the top, -80 at the bottom.
class SignalBand {
 public:
  enum class View : uint8_t { Scope, Spectrum };

  // A transparent box holding the trace, sized `width` x `height`; the
  // caller positions it, and may make it take touches (a swipe target).
  lv_obj_t *create(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, lv_color_t trace,
                   lv_color_t zeroLine) {
    box_ = lv_obj_create(parent);
    lv_obj_remove_style_all(box_);
    lv_obj_set_size(box_, width, height);
    lv_obj_clear_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box_, LV_OBJ_FLAG_CLICKABLE);
    trace_.create(box_, 0, 0, width, height, trace, zeroLine);
    samples_.resize(kSampleCapacity);
    applyView();
    return box_;
  }

  // The screen that owned the objects is being deleted.
  void detach() {
    box_ = nullptr;
    trace_.detach();
  }

  // Gives the ~40 KB of sample and FFT buffers back while nothing shows
  // the band; they come from PSRAM.
  void release() {
    std::vector<int16_t>().swap(samples_);
    spectrum_.release();
  }

  lv_obj_t *raw() const { return box_; }
  View view() const { return view_; }

  void setView(View view) {
    view_ = view;
    applyView();
  }

  // Where the caller reads the newest samples to, oldest first.
  int16_t *buffer() { return samples_.data(); }
  size_t capacity() const { return samples_.size(); }

  // One frame of the scope, its timebase chosen for `hz`, its level range
  // for `levelDb`.
  void showScope(size_t count, uint32_t sampleRate, float hz, int levelDb) {
    if (!box_ || count == 0 || sampleRate == 0) return;
    updateScale(hz, levelDb);
    const float span = static_cast<float>(scale_.timebaseUs()) *
                       static_cast<float>(sampleRate) / 1000000.0f;
    signal::TriggeredScope::traceWindow(samples_.data(), count, span, points_.data(),
                                        points_.size());
    // Held to the level range's top, not to the level itself: within a
    // range, +6 dB draws twice as tall.
    const auto fullScale = std::max<int32_t>(
        1, static_cast<int32_t>(signal::ToneSettings::dbToLinear(scale_.topDb()) * 32767.0f));
    trace_.setSamples(points_.data(), points_.size(), fullScale);
  }

  // Flat on the zero line: nothing is sounding.
  void clearScope() { trace_.clear(); }

  // One frame of the spectrum; count == 0 only lets the levels fall.
  void showSpectrum(size_t count, uint32_t sampleRate, uint32_t dtMs) {
    if (!box_) return;
    spectrum_.update(count ? samples_.data() : nullptr, count, sampleRate, dtMs);
    const auto &levels = spectrum_.levels();
    for (size_t i = 0; i < points_.size() && i < levels.size(); ++i) {
      points_[i] = static_cast<int16_t>(std::clamp<float>(
          levels[i] * 10.0f, static_cast<float>(kBottomDeci), static_cast<float>(kTopDeci)));
    }
    trace_.setPoints(points_.data(), points_.size(), kBottomDeci, kTopDeci);
  }

  // Brings the scope's scales up to date without drawing, for a label.
  void updateScale(float hz, int levelDb) { scale_.update(hz, levelDb); }

  // The scope's scales ("5 ms · -20 dB"), or the spectrum's axis.
  void label(char *out, size_t size) const {
    if (view_ == View::Scope) {
      scale_.label(out, size);
    } else {
      snprintf(out, size, "20 Hz \xE2\x80\x93 20 kHz \xC2\xB7 0 to -80 dB");
    }
  }

  // The loudest of the newest `count` samples, in whole dBFS, no lower
  // than -60 -- what a range follows when there is no set level.
  static int peakDb(const int16_t *samples, size_t count) {
    int32_t peak = 0;
    for (size_t i = 0; i < count; ++i) {
      peak = std::max<int32_t>(peak, std::abs(static_cast<int32_t>(samples[i])));
    }
    if (peak == 0) return kQuietestDb;
    const float db = 20.0f * std::log10(static_cast<float>(peak) / 32768.0f);
    return std::max(kQuietestDb, static_cast<int>(std::ceil(db)));
  }

 private:
  // The output stage's whole ring (AudioOutputStage::kSampleRingSize):
  // the scope's longest timebase needs most of it.
  static constexpr size_t kSampleCapacity = 4096;
  // The spectrum's band in the tenths of a dB it is drawn in.
  static constexpr int32_t kTopDeci = 0;
  static constexpr int32_t kBottomDeci = -800;
  static constexpr int kQuietestDb = -60;

  void applyView() {
    trace_.setZeroLineVisible(view_ == View::Scope);
    if (view_ == View::Scope) {
      trace_.clear();
      return;
    }
    spectrum_.reset();
    points_.fill(static_cast<int16_t>(kBottomDeci));
    trace_.setPoints(points_.data(), points_.size(), kBottomDeci, kTopDeci);
  }

  lv_obj_t *box_ = nullptr;
  ui_widgets::ScopeTrace trace_;
  View view_ = View::Scope;
  signal::ScopeScale scale_;
  signal::Spectrum spectrum_{ui_widgets::ScopeTrace::kPoints};
  std::vector<int16_t> samples_;
  std::array<int16_t, ui_widgets::ScopeTrace::kPoints> points_{};
};

}  // namespace drehklang::ui
