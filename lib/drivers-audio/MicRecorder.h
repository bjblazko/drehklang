#pragma once

#include <driver/i2s_pdm.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "DacArbiter.h"
#include "DictaphonePorts.h"
#include "MicConditioner.h"
#include "SdWavWriter.h"

namespace drehklang::drivers {

// The board's PDM microphone (GPIO 45/46, device.md) for the dictaphone
// (ADR 0032). PDM RX exists on I2S0 only, so the microphone is a third
// owner of that port next to the player and the tone output: listening
// claims it, which parks a playing track.
//
// Its own task on core 0 reads 20 ms blocks, conditions them (DC filter,
// gain), notes every sample in the output stage's ring for the band, and
// hands them to the WAV writer while recording.
class MicRecorder : public dictaphone::Recorder, public playback::DacOwner {
 public:
  static constexpr uint32_t kSampleRate = 16000;

  MicRecorder(playback::DacArbiter &dac, SdWavWriter &writer) : dac_(dac), writer_(writer) {}

  // Starts the task. Call once, after the writer's begin().
  void begin();

  // dictaphone::Recorder -- main loop only.
  bool listen() override;
  void close() override;
  bool record(const std::string &path) override;
  void stopRecording() override;
  bool recording() const override { return recording_.load() && writer_.busy(); }
  bool saving() const override { return writer_.busy(); }
  void pauseRecording() override { paused_.store(recording()); }
  void resumeRecording() override { paused_.store(false); }
  bool paused() const override { return paused_.load() && recording(); }
  uint32_t recordedMs() const override {
    return static_cast<uint32_t>(static_cast<uint64_t>(recordedSamples_.load()) * 1000 /
                                 kSampleRate);
  }
  bool takeWriteFailure() override;

  // signal::SampleSource -- what the microphone heard, for the band.
  size_t readRecent(int16_t *dst, size_t maxSamples) override;

  // playback::DacOwner -- the player or the tone output wants I2S0.
  void releaseDac() override;

 private:
  // 20 ms at 16 kHz.
  static constexpr size_t kBlockSamples = 320;
  // This board's microphone speaks at -45..-33 dBFS (device.md); +18 dB
  // brings speech to where music sits without clipping a raised voice.
  static constexpr float kGain = 8.0f;

  bool openChannel();
  void closeChannel();
  [[noreturn]] void taskLoop();
  static void taskTrampoline(void *self);

  playback::DacArbiter &dac_;
  SdWavWriter &writer_;
  // Guards channel_ and conditioner_ between the main loop and the task.
  SemaphoreHandle_t mutex_ = nullptr;
  i2s_chan_handle_t channel_ = nullptr;
  dictaphone::MicConditioner conditioner_{kGain};
  int16_t raw_[kBlockSamples] = {};
  int16_t clean_[kBlockSamples] = {};
  std::atomic<bool> recording_{false};
  std::atomic<bool> paused_{false};
  std::atomic<uint32_t> recordedSamples_{0};
};

}  // namespace drehklang::drivers
