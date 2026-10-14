#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace drehklang::dictaphone {

// What the microphone's raw PCM needs before anyone hears or sees it
// (ADR 0032): this board's PDM part sits at a DC offset of ~1375 and speaks
// at -45..-33 dBFS (device.md). A one-pole high-pass (~13 Hz at 16 kHz)
// takes the offset away, a fixed gain brings speech up, and anything
// louder clips rather than wraps.
class MicConditioner {
 public:
  explicit MicConditioner(float gain) : gain_(gain) {}

  // Forgets the previous block: the next one starts from its own first
  // sample, so opening the microphone does not begin with a thump.
  void reset() { primed_ = false; }

  void process(const int16_t *in, int16_t *out, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      const float x = in[i];
      if (!primed_) {
        lastIn_ = x;
        lastOut_ = 0.0f;
        primed_ = true;
      }
      lastOut_ = x - lastIn_ + kPole * lastOut_;
      lastIn_ = x;
      const float y = lastOut_ * gain_;
      out[i] = static_cast<int16_t>(std::clamp(y, -32767.0f, 32767.0f));
    }
  }

 private:
  static constexpr float kPole = 0.995f;

  float gain_;
  bool primed_ = false;
  float lastIn_ = 0.0f;
  float lastOut_ = 0.0f;
};

}  // namespace drehklang::dictaphone
