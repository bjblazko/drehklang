// ESP32-audioI2S's weak hooks, on its decode task. Both run once per DMA
// chunk of interleaved left/right full-scale 32-bit samples.
//
// Deliberately in a file that does not include <Audio.h>: that header
// declares the hooks weak, and a definition after a weak declaration is
// weak too -- the linker then kept the library's empty stubs instead of
// these (found with nm, 2026-10-05). setHoldOutput() is what pulls this
// object into the link: a weak reference alone never would (AGENTS.md).

#include "AudioHooks.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "AudioGain.h"
#include "AudioOutputStage.h"
#include "AudioTap.h"
#include "PeakLimiter.h"

#ifdef DREHKLANG_EQ_DEBUG
#include <esp_timer.h>
#endif

namespace drehklang::drivers {
namespace {
std::atomic<bool> g_holdOutput{false};
std::atomic<bool> g_producedSinceHold{false};
std::atomic<drehklang::bluetooth::AudioTap *> g_bluetoothTap{nullptr};
std::atomic<uint32_t> g_decoderRate{44100};
// Internal RAM: the decode task runs it on every chunk.
signal::GraphicEqualizer g_equalizer;
// Decode task only, like the equalizer.
signal::PeakLimiter g_makeupLimiter;
#ifdef DREHKLANG_EQ_DEBUG
std::atomic<uint32_t> g_worstEqualizerUs{0};
std::atomic<uint32_t> g_worstEqualizerWords{0};
std::atomic<uint32_t> g_totalEqualizerUs{0};
std::atomic<uint32_t> g_totalEqualizerWords{0};
#endif
}  // namespace

signal::GraphicEqualizer &musicEqualizer() { return g_equalizer; }

// All of the headroom back, at every volume, through a peak limiter
// (ADR 0029): the rest of the music keeps its level; a lifted band that
// would pass full scale dips the gain for a moment instead of clipping.
void applyEqualizerMakeup(int32_t *samples, size_t words) {
  g_makeupLimiter.process(samples, words, g_equalizer.headroom(), decoderRate());
}

#ifdef DREHKLANG_EQ_DEBUG
uint32_t takeWorstEqualizerUs(uint32_t &words) {
  words = g_worstEqualizerWords.load(std::memory_order_relaxed);
  return g_worstEqualizerUs.exchange(0, std::memory_order_relaxed);
}

uint32_t takeTotalEqualizerUs(uint32_t &words) {
  words = g_totalEqualizerWords.exchange(0, std::memory_order_relaxed);
  return g_totalEqualizerUs.exchange(0, std::memory_order_relaxed);
}

namespace {
void runEqualizerMeasured(int32_t *samples, size_t words, uint32_t rate) {
  const int64_t start = esp_timer_get_time();
  g_equalizer.process(samples, words, rate);
  const auto took = static_cast<uint32_t>(esp_timer_get_time() - start);
  g_totalEqualizerUs.fetch_add(took, std::memory_order_relaxed);
  g_totalEqualizerWords.fetch_add(static_cast<uint32_t>(words), std::memory_order_relaxed);
  if (took > g_worstEqualizerUs.load(std::memory_order_relaxed)) {
    g_worstEqualizerUs.store(took, std::memory_order_relaxed);
    g_worstEqualizerWords.store(static_cast<uint32_t>(words), std::memory_order_relaxed);
  }
}
}  // namespace
#endif

void setHoldOutput(bool hold) {
  if (hold) g_producedSinceHold.store(false, std::memory_order_relaxed);
  g_holdOutput.store(hold, std::memory_order_relaxed);
}

bool decoderProducedSinceHold() { return g_producedSinceHold.load(std::memory_order_relaxed); }

bool holdingOutput() { return g_holdOutput.load(std::memory_order_relaxed); }

void setBluetoothTap(bluetooth::AudioTap *tap) {
  g_bluetoothTap.store(tap, std::memory_order_release);
}

bluetooth::AudioTap *bluetoothTap() { return g_bluetoothTap.load(std::memory_order_acquire); }

void setDecoderRate(uint32_t rate) { g_decoderRate.store(rate, std::memory_order_relaxed); }

uint32_t decoderRate() { return g_decoderRate.load(std::memory_order_relaxed); }

}  // namespace drehklang::drivers

// Before the volume: the equalizer (ADR 0029), then what the spectrum
// analyses (ADR 0009) -- after the equalizer, so it shows what is heard,
// and before the volume, so it never has to divide the volume back out.
void audio_process_raw_samples(int32_t *samples, int16_t words) {
  using namespace drehklang::drivers;
  if (holdingOutput()) {
    if (words > 0) g_producedSinceHold.store(true, std::memory_order_relaxed);
    return;
  }
  const auto count = static_cast<size_t>(words);
#ifdef DREHKLANG_EQ_DEBUG
  runEqualizerMeasured(samples, count, decoderRate());
#else
  g_equalizer.process(samples, count, decoderRate());
#endif
  audioOutputStage().noteStereo32(samples, count, g_equalizer.headroom());
}

// After the volume: the equalizer's headroom given back as far as the
// volume leaves room (ADR 0029), the sleep timer's fade (ADR 0015),
// silence while a resume position is still being applied, and a copy for
// Bluetooth headphones -- exactly what the jack gets (ADR 0027).
void audio_process_i2s(int32_t *samples, int16_t words, bool *continueI2S) {
  *continueI2S = true;
  if (drehklang::drivers::holdingOutput()) {
    std::fill(samples, samples + words, 0);
    return;
  }
  drehklang::drivers::applyEqualizerMakeup(samples, static_cast<size_t>(words));
  const uint16_t gain = drehklang::drivers::audioOutputStage().outputGain();
  if (gain < drehklang::playback::AudioGain::kUnityOutputGain) {
    for (int16_t i = 0; i < words; ++i) {
      samples[i] = drehklang::playback::AudioGain::applyOutputGain(samples[i], gain);
    }
  }
  if (auto *tap = drehklang::drivers::bluetoothTap()) {
    tap->pushStereo32(samples, static_cast<size_t>(words), drehklang::drivers::decoderRate());
  }
}
