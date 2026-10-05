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

namespace drehklang::drivers {
namespace {
std::atomic<bool> g_holdOutput{false};
std::atomic<bool> g_producedSinceHold{false};
std::atomic<drehklang::bluetooth::AudioTap *> g_bluetoothTap{nullptr};
std::atomic<uint32_t> g_decoderRate{44100};
}  // namespace

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

// Before the volume: what the spectrum analyses (ADR 0009), so it never
// has to divide the volume back out.
void audio_process_raw_samples(int32_t *samples, int16_t words) {
  if (drehklang::drivers::holdingOutput()) {
    if (words > 0) drehklang::drivers::g_producedSinceHold.store(true, std::memory_order_relaxed);
    return;
  }
  drehklang::drivers::audioOutputStage().noteStereo32(samples, static_cast<size_t>(words));
}

// After the volume: the sleep timer's fade (ADR 0015), silence while a
// resume position is still being applied, and a copy for Bluetooth
// headphones -- exactly what the jack gets (ADR 0027).
void audio_process_i2s(int32_t *samples, int16_t words, bool *continueI2S) {
  *continueI2S = true;
  if (drehklang::drivers::holdingOutput()) {
    std::fill(samples, samples + words, 0);
    return;
  }
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
