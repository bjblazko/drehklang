#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace drehklang::playback {

// The one place Drehklang decides how loud a sample is (ADR 0026). The
// decoder library takes its volume curve from here (volumeCurveDb), the
// sleep timer's fade is applied after it (applyOutputGain), and a game's
// blips use the same steps (applyVolume) -- so all three agree.
class AudioGain {
 public:
  // Output gain is 0..4096, where 4096 is unity -- the sleep timer fades
  // with it (ADR 0015) because the 22 volume steps are far too coarse.
  static constexpr uint16_t kUnityOutputGain = 4096;
  static constexpr uint8_t kMaxVolumeStep = 21;

  // Linear gain per volume step, as entry/64. The curve Drehklang has
  // always had (it was ESP32-audioI2S 2.3.0's table); kept so the knob
  // feels the same after the library upgrade.
  static constexpr uint8_t kVolumeTable[kMaxVolumeStep + 1] = {
      0, 1, 2, 3, 4, 6, 8, 10, 12, 14, 17, 20, 23, 27, 30, 34, 38, 43, 48, 52, 58, 64};

  // The library's volume curve hook: `t` is volume/steps in 0..1, the
  // result a level in dB. Step 0 never reaches here (the library mutes it
  // itself), so log10 of 0 cannot happen.
  static float volumeCurveDb(float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    const auto step = static_cast<uint8_t>(std::lround(clamped * kMaxVolumeStep));
    const uint8_t entry = std::max<uint8_t>(kVolumeTable[step], 1);
    return 20.0f * std::log10(entry / 64.0f);
  }

  // The sleep timer's fade on a full-scale 32-bit sample, after the
  // library has applied the volume.
  static int32_t applyOutputGain(int32_t sample, uint16_t outputGain) {
    const int64_t gain = std::min<uint16_t>(outputGain, kUnityOutputGain);
    return static_cast<int32_t>(static_cast<int64_t>(sample) * gain / kUnityOutputGain);
  }

  // Volume and fade on a 16-bit sample -- a game's blips.
  static int16_t applyVolume(int16_t sample, uint8_t volumeStep, uint16_t outputGain) {
    const int32_t gain = std::min<int32_t>(outputGain, kUnityOutputGain);
    const int32_t step = kVolumeTable[std::min(volumeStep, kMaxVolumeStep)];
    return static_cast<int16_t>(std::clamp<int32_t>(
        static_cast<int32_t>(sample) * step / 64 * gain / kUnityOutputGain, INT16_MIN,
        INT16_MAX));
  }
};

}  // namespace drehklang::playback
