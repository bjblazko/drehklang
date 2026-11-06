#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "StereoLevels.h"
#include "VuNeedle.h"

namespace drehklang::visualizer {

// Sets the meters' level per track, the way one sets a deck's recording
// level before a take (ADR 0034). Modern masters run their loud passages
// near RMS -6 dBFS and pinned both meters at their stops; quiet old
// recordings barely moved them.
//
// For the first kLearnMs of sound in a track it follows the loud
// passages -- RMS and peak smoothed over about a second, so one drum hit
// is not the track's level -- and brings them to 0 VU and just below the
// bars' red. Then the gain stays for the rest of the track, so the
// dynamics within it stay visible. Silence does not count as learning, and
// until kStartMs of sound the last track's gain holds, so the next song of
// an album does not jump. The volume plays no part: the levels are taken
// before it. Pure logic, explicit time, host-tested.
class AutoLevel {
 public:
  static constexpr float kRmsTargetDb = vu::kRefDbfs;  // 0 VU
  static constexpr float kPeakTargetDb = -4.0f;  // just below the red at -3
  static constexpr float kMaxGainDb = 15.0f;     // either way
  static constexpr uint32_t kLearnMs = 6000;
  static constexpr uint32_t kStartMs = 1000;
  static constexpr float kSilenceDb = -50.0f;
  static constexpr float kSmoothingMs = 1000.0f;

  void startTrack() {
    soundMs_ = 0;
    rms_ = Follower{};
    peak_ = Follower{};
  }

  signal::StereoLevels apply(const signal::StereoLevels &in, uint32_t dtMs) {
    learn(in, dtMs);
    signal::StereoLevels out = in;
    for (int ch = 0; ch < 2; ++ch) {
      out.rms[ch] *= rmsGain_;
      out.peak[ch] *= peakGain_;
    }
    return out;
  }

 private:
  // A level smoothed from its first sound on, and the loudest it reached.
  struct Follower {
    float smooth = 0.0f;
    float loudest = 0.0f;

    void add(float x, float alpha, bool first) {
      smooth = first ? x : smooth + alpha * (x - smooth);
      loudest = std::max(loudest, smooth);
    }
  };

  void learn(const signal::StereoLevels &in, uint32_t dtMs) {
    const float rms = (in.rms[0] + in.rms[1]) / 2.0f;
    if (soundMs_ >= kLearnMs || !(rms > fromDb(kSilenceDb))) return;
    const float alpha = 1.0f - std::exp(-static_cast<float>(dtMs) / kSmoothingMs);
    const bool first = soundMs_ == 0;
    rms_.add(rms, alpha, first);
    peak_.add(std::max(in.peak[0], in.peak[1]), alpha, first);
    soundMs_ += dtMs;
    if (soundMs_ < kStartMs) return;
    rmsGain_ = gainFor(rms_.loudest, kRmsTargetDb);
    peakGain_ = gainFor(peak_.loudest, kPeakTargetDb);
  }

  static float gainFor(float loudest, float targetDb) {
    const float limit = fromDb(kMaxGainDb);
    return std::clamp(fromDb(targetDb) / loudest, 1.0f / limit, limit);
  }

  static float fromDb(float db) { return std::pow(10.0f, db / 20.0f); }

  uint32_t soundMs_ = 0;
  Follower rms_;
  Follower peak_;
  float rmsGain_ = 1.0f;
  float peakGain_ = 1.0f;
};

}  // namespace drehklang::visualizer
