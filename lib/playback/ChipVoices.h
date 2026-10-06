#pragma once

#include <atomic>
#include <cstdint>

namespace drehklang::playback {

// One sound effect on the third voice (ADR 0030): a square wave or noise
// whose pitch slides from startHz to endHz and whose level decays to
// nothing over durationMs. Every one-shot in Circuit is one of these.
struct ChipEffect {
  uint16_t startHz;
  uint16_t endHz;
  uint16_t durationMs;
  int16_t level;
  bool noise;
};

// A small three-voice sound chip for Circuit (ADR 0030), in the spirit of
// the SID and Paula rather than ToneGenerator's single blip:
//
//   1. engine -- a narrow pulse wave whose pitch follows the revs. Held,
//      and changed live: pitch and level glide, the phase never resets,
//      so a gear change is a drop in pitch rather than a click.
//   2. noise  -- a held LFSR rumble (off the road) or hiss (tyre squeal).
//   3. effect -- one-shots with a pitch sweep and a decaying level.
//
// Same handover as ToneGenerator: the main loop (core 1) writes atomics,
// the audio task (core 0) owns everything else and reads them per sample.
class ChipVoices {
 public:
  // Held voices: set again whenever the game's state changes. Level 0 is
  // off.
  void setEngine(uint16_t hz, int16_t level) {
    engineTarget_.store(pack(hz, level), std::memory_order_relaxed);
  }
  void setNoise(uint16_t clockHz, int16_t level) {
    noiseTarget_.store(pack(clockHz, level), std::memory_order_relaxed);
  }

  // A one-shot on the effect voice; replaces one still sounding, which is
  // what a crash straight after a bump should do.
  void play(const ChipEffect &effect) {
    if (effect.durationMs == 0 || effect.startHz == 0) return;
    effectRequest_ = effect;
    effectSeq_.fetch_add(1, std::memory_order_release);
  }

  // Everything off, at once: leaving the game.
  void silence() {
    engineTarget_.store(0, std::memory_order_relaxed);
    noiseTarget_.store(0, std::memory_order_relaxed);
    // A pending effect is dropped too: marked as already seen.
    seenEffect_.store(effectSeq_.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);
    effectRunning_.store(false, std::memory_order_relaxed);
    killSeq_.fetch_add(1, std::memory_order_release);
  }

  bool active() const {
    return levelOf(engineTarget_.load(std::memory_order_relaxed)) > 0 ||
           levelOf(noiseTarget_.load(std::memory_order_relaxed)) > 0 ||
           effectRunning_.load(std::memory_order_relaxed) ||
           effectSeq_.load(std::memory_order_relaxed) !=
               seenEffect_.load(std::memory_order_relaxed);
  }

  // The next mono sample at this rate. Audio side only.
  int16_t nextSample(uint32_t sampleRate) {
    if (sampleRate == 0) return 0;
    pollRequests(sampleRate);
    const int32_t mix = engineSample(sampleRate) + noiseSample(sampleRate) +
                        effectSample(sampleRate);
    if (mix > kMaxSample) return kMaxSample;
    if (mix < -kMaxSample) return -kMaxSample;
    return static_cast<int16_t>(mix);
  }

 private:
  static constexpr int32_t kMaxSample = 32767;
  // The engine's pulse is high for a quarter of its cycle: thinner and
  // more of a snarl than a square.
  static constexpr uint32_t kPulseWidth = 0x40000000u;
  // How fast a held voice's pitch and level follow their target: about
  // 1/256 of the gap per sample, ~12 ms at 22 kHz. Fast enough to follow
  // the revs, slow enough that a jump is a slide.
  static constexpr int kGlideShift = 8;
  // A slight vibrato on the engine, so it reads as a motor rather than a
  // test tone: +-1/64 of the pitch at ~6 Hz.
  static constexpr uint32_t kVibratoHz = 6;
  static constexpr int kVibratoDepthShift = 6;
  static constexpr uint16_t kLfsrSeed = 0xACE1u;
  static constexpr uint16_t kLfsrTaps = 0xB400u;

  static uint32_t pack(uint16_t hz, int16_t level) {
    const uint16_t l = level < 0 ? 0 : static_cast<uint16_t>(level);
    return (static_cast<uint32_t>(hz) << 16) | l;
  }
  static uint16_t hzOf(uint32_t packed) { return static_cast<uint16_t>(packed >> 16); }
  static int32_t levelOf(uint32_t packed) { return static_cast<int32_t>(packed & 0xFFFF); }

  // Phase increment for a frequency in 1/256 Hz, as a 32-bit fraction of
  // a cycle per sample.
  static uint32_t increment(uint32_t hz256, uint32_t sampleRate) {
    return static_cast<uint32_t>((static_cast<uint64_t>(hz256) << 24) / sampleRate);
  }

  static uint16_t stepLfsr(uint16_t lfsr) {
    return static_cast<uint16_t>((lfsr >> 1) ^ (-(lfsr & 1u) & kLfsrTaps));
  }

  static void glide(int32_t &value, int32_t target) {
    const int32_t gap = target - value;
    int32_t step = gap >> kGlideShift;
    if (step == 0 && gap != 0) step = gap > 0 ? 1 : -1;
    value += step;
  }

  void pollRequests(uint32_t sampleRate) {
    const uint32_t kill = killSeq_.load(std::memory_order_acquire);
    if (kill != seenKill_) {
      seenKill_ = kill;
      engineHz256_ = engineLevel_ = noiseLevel_ = 0;
      effectRemaining_ = 0;
      effectRunning_.store(false, std::memory_order_relaxed);
    }
    const uint32_t seq = effectSeq_.load(std::memory_order_acquire);
    if (seq != seenEffect_.load(std::memory_order_relaxed)) {
      // Running before seen, so active() never reads false in between.
      effectRunning_.store(true, std::memory_order_relaxed);
      seenEffect_.store(seq, std::memory_order_relaxed);
      const ChipEffect e = effectRequest_;
      effectTotal_ = sampleRate * e.durationMs / 1000;
      if (effectTotal_ == 0) effectTotal_ = 1;
      effectRemaining_ = effectTotal_;
      effectStartHz256_ = static_cast<int32_t>(e.startHz) << 8;
      effectEndHz256_ = static_cast<int32_t>(e.endHz) << 8;
      effectLevel_ = e.level;
      effectNoise_ = e.noise;
      effectPhase_ = 0;
      effectHigh_ = true;
    }
  }

  int32_t engineSample(uint32_t sampleRate) {
    const uint32_t target = engineTarget_.load(std::memory_order_relaxed);
    const int32_t targetLevel = levelOf(target);
    // From silence the pitch starts at its target, so a start is not a
    // swoop up from 0 Hz.
    if (engineLevel_ == 0 && targetLevel > 0) {
      engineHz256_ = static_cast<int32_t>(hzOf(target)) << 8;
    }
    glide(engineHz256_, static_cast<int32_t>(hzOf(target)) << 8);
    glide(engineLevel_, targetLevel);
    if (engineLevel_ == 0 || engineHz256_ <= 0) return 0;

    vibratoPhase_ += increment(kVibratoHz << 8, sampleRate);
    const int32_t wobble = (vibratoPhase_ & 0x80000000u) ? 1 : -1;
    const uint32_t hz256 = static_cast<uint32_t>(
        engineHz256_ + wobble * (engineHz256_ >> kVibratoDepthShift));
    enginePhase_ += increment(hz256, sampleRate);
    return enginePhase_ < kPulseWidth ? engineLevel_ : -engineLevel_;
  }

  int32_t noiseSample(uint32_t sampleRate) {
    const uint32_t target = noiseTarget_.load(std::memory_order_relaxed);
    glide(noiseLevel_, levelOf(target));
    if (noiseLevel_ == 0 || hzOf(target) == 0) return 0;
    const uint32_t before = noisePhase_;
    noisePhase_ += increment(static_cast<uint32_t>(hzOf(target)) << 8, sampleRate);
    if (noisePhase_ < before) noiseLfsr_ = stepLfsr(noiseLfsr_);
    return (noiseLfsr_ & 1u) ? noiseLevel_ : -noiseLevel_;
  }

  int32_t effectSample(uint32_t sampleRate) {
    if (effectRemaining_ == 0) return 0;
    const uint32_t done = effectTotal_ - effectRemaining_;
    --effectRemaining_;
    const int64_t span = effectEndHz256_ - effectStartHz256_;
    const int32_t hz256 = effectStartHz256_ +
                          static_cast<int32_t>(span * done / effectTotal_);
    if (effectRemaining_ == 0) effectRunning_.store(false, std::memory_order_relaxed);
    const int32_t level = static_cast<int32_t>(
        static_cast<int64_t>(effectLevel_) * effectRemaining_ / effectTotal_);
    // A square toggles twice a cycle; noise steps its register once.
    const uint32_t rate = effectNoise_ ? static_cast<uint32_t>(hz256)
                                       : static_cast<uint32_t>(hz256) * 2;
    const uint32_t before = effectPhase_;
    effectPhase_ += increment(rate, sampleRate);
    if (effectPhase_ < before) {
      if (effectNoise_) {
        effectLfsr_ = stepLfsr(effectLfsr_);
        effectHigh_ = (effectLfsr_ & 1u) != 0;
      } else {
        effectHigh_ = !effectHigh_;
      }
    }
    return effectHigh_ ? level : -level;
  }

  // Main loop -> audio task.
  std::atomic<uint32_t> engineTarget_{0};
  std::atomic<uint32_t> noiseTarget_{0};
  std::atomic<uint32_t> effectSeq_{0};
  std::atomic<uint32_t> killSeq_{0};
  std::atomic<bool> effectRunning_{false};
  // Written before effectSeq_ is released, read after it is acquired.
  ChipEffect effectRequest_{};

  // Audio task writes, active() reads: what has been picked up.
  std::atomic<uint32_t> seenEffect_{0};

  // Audio task only.
  uint32_t seenKill_ = 0;
  int32_t engineHz256_ = 0;
  int32_t engineLevel_ = 0;
  uint32_t enginePhase_ = 0;
  uint32_t vibratoPhase_ = 0;
  int32_t noiseLevel_ = 0;
  uint32_t noisePhase_ = 0;
  uint16_t noiseLfsr_ = kLfsrSeed;
  uint32_t effectTotal_ = 0;
  uint32_t effectRemaining_ = 0;
  int32_t effectStartHz256_ = 0;
  int32_t effectEndHz256_ = 0;
  int32_t effectLevel_ = 0;
  bool effectNoise_ = false;
  bool effectHigh_ = true;
  uint32_t effectPhase_ = 0;
  uint16_t effectLfsr_ = kLfsrSeed;
};

}  // namespace drehklang::playback
