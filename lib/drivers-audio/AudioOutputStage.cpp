#include "AudioOutputStage.h"

#include <algorithm>

#include "AudioGain.h"

namespace drehklang::drivers {

void AudioOutputStage::setOutputGain(uint16_t gain) {
  outputGain_.store(std::min<uint16_t>(gain, playback::AudioGain::kUnityOutputGain),
                    std::memory_order_relaxed);
}

void AudioOutputStage::noteMonoSample(int16_t mono) {
  const uint32_t written = samplesWritten_.load(std::memory_order_relaxed);
  ring_[written & (kSampleRingSize - 1)] = mono;
  samplesWritten_.store(written + 1, std::memory_order_relaxed);
}

void AudioOutputStage::noteStereo32(const int32_t *interleaved, size_t words) {
  for (size_t i = 0; i + 1 < words; i += 2) {
    const int64_t sum = static_cast<int64_t>(interleaved[i]) + interleaved[i + 1];
    noteMonoSample(static_cast<int16_t>(sum >> 17));  // Average, then top 16 bits.
  }
}

playback::SampleWindow AudioOutputStage::readRecentSamples(int16_t *dst,
                                                           size_t maxSamples,
                                                           uint32_t sampleRate) {
  const uint32_t written = samplesWritten_.load(std::memory_order_relaxed);
  if (written == lastReadCount_) return {};  // Paused or between tracks.
  lastReadCount_ = written;

  const size_t count = std::min<size_t>({maxSamples, kSampleRingSize, written});
  for (size_t i = 0; i < count; ++i) {
    dst[i] = ring_[(written - count + i) & (kSampleRingSize - 1)];
  }
  playback::SampleWindow window;
  window.count = count;
  window.sampleRate = sampleRate;
  window.gain = volumeStep() > 0 && outputGain() > 0 ? 1.0f : 0.0f;
  return window;
}

AudioOutputStage &audioOutputStage() {
  static AudioOutputStage stage;
  return stage;
}

}  // namespace drehklang::drivers
