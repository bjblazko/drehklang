#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "PlaybackDriver.h"

namespace drehklang::drivers {

// What the DAC's current owner (ADR 0026) shares with the rest of the
// app: the volume step and the sleep timer's output gain it plays at, and
// a ring of what it last played, for the spectrum and the tone
// generator's scope.
//
// The ring has a single producer (the DAC's owner, core 0 -- only one
// owner exists at a time) and a single consumer (the main loop, core 1);
// the producer only stores a sample and bumps a counter, so there is no
// lock in the hot path -- a reader racing a writer can at worst see its
// oldest sample replaced by a newer one, which is invisible in a spectrum.
class AudioOutputStage {
 public:
  void setVolumeStep(uint8_t step) { volumeStep_.store(step, std::memory_order_relaxed); }
  uint8_t volumeStep() const { return volumeStep_.load(std::memory_order_relaxed); }
  void setOutputGain(uint16_t gain);
  uint16_t outputGain() const { return outputGain_.load(std::memory_order_relaxed); }

  void noteMonoSample(int16_t mono);
  // The decoder library's samples: interleaved left/right, full-scale
  // 32-bit, before volume -- so the spectrum needs no gain divided out.
  // `gain` undoes the equalizer's headroom (ADR 0029), so the pictures
  // show the music at its own level.
  void noteStereo32(const int32_t *interleaved, size_t words, float gain = 1.0f);

  // Samples kept for whoever reads them back: the Now Playing spectrum
  // takes its 1024, the tone generator's scope up to all of them -- two
  // periods of 20 Hz at 48 kHz need 4800, and 4096 shows most of that
  // (ADR 0024). Power of two.
  static constexpr size_t kSampleRingSize = 4096;

  // `gain` in the result is 1 while anything is audible and 0 when muted:
  // the ring holds samples from before the volume.
  playback::SampleWindow readRecentSamples(int16_t *dst, size_t maxSamples,
                                           uint32_t sampleRate);

 private:
  int16_t ring_[kSampleRingSize] = {};
  std::atomic<uint32_t> samplesWritten_{0};
  std::atomic<uint16_t> outputGain_{4096};
  std::atomic<uint8_t> volumeStep_{0};
  uint32_t lastReadCount_ = 0;
};

// One instance; the library's weak hooks have no other way in.
AudioOutputStage &audioOutputStage();

}  // namespace drehklang::drivers
