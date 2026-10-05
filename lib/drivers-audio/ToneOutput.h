#pragma once

#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstddef>
#include <cstdint>

#include "BlipPlayer.h"
#include "DacArbiter.h"
#include "GeneratorControl.h"
#include "Oscillator.h"
#include "SampleSource.h"
#include "ToneGenerator.h"

namespace drehklang::drivers {

// A game's blips (ADR 0022) and the tone generator (ADR 0024). Neither
// plays over music: the first sound claims the DAC from the player
// (ADR 0026), which parks its track, and this class opens its own I2S
// channel until the player claims the DAC back.
//
// Its own FreeRTOS task on core 0 does the writing, rather than loop():
// a channel write blocks until the DMA buffers take the frames, and
// blocking loop() is what makes LVGL stutter and trips the loop watchdog
// (AGENTS.md).
class ToneOutput : public games::BlipPlayer,
                   public signal::GeneratorOutput,
                   public signal::SampleSource,
                   public playback::DacOwner {
 public:
  explicit ToneOutput(playback::DacArbiter &dac) : dac_(dac) {}

  // Starts the task. Call once.
  void begin();

  // games::BlipPlayer -- main loop only (they may claim the DAC).
  void blip(uint16_t frequencyHz, uint16_t durationMs) override;
  void noise(uint16_t clockHz, uint16_t durationMs, int16_t level) override;
  // Drops anything pending and stops a sounding blip, for leaving a game.
  void silence() override { tone_.silence(); }

  // signal::GeneratorOutput -- main loop only. The task picks each
  // change up at its next chunk.
  void apply(const signal::OscillatorParams &params) override {
    control_.publish(params);
  }
  void start() override;
  void stop() override { control_.setRunning(false); }

  // signal::SampleSource -- what this task last wrote to the DAC, for the
  // tone generator's scope. Main loop only.
  size_t readRecent(int16_t *dst, size_t maxSamples) override;

  // playback::DacOwner -- the player wants the DAC back.
  void releaseDac() override;

 private:
  static constexpr uint32_t kBlipSampleRate = 22050;
  static constexpr size_t kChunkFrames = 128;
  // How often the task looks for something to play. A blip is a response
  // to something the player did, so this is latency, not idling: measured
  // trigger-to-write was under 2.5ms with this (2026-09-18).
  static constexpr uint32_t kPollMs = 2;

  void claimDac(uint32_t rate);
  bool openChannel(uint32_t rate);
  void closeChannel();
  bool writeChunk(uint32_t rate);
  bool feedBluetoothSilence();
  [[noreturn]] void taskLoop();
  static void taskTrampoline(void *self);

  playback::DacArbiter &dac_;
  // Guards channel_ and channelRate_ between the main loop (claim,
  // release) and the task (writes).
  SemaphoreHandle_t mutex_ = nullptr;
  i2s_chan_handle_t channel_ = nullptr;
  uint32_t channelRate_ = 0;

  playback::ToneGenerator tone_;
  // The generator's side of the handover and its voice. The oscillator is
  // touched only by the task.
  signal::GeneratorControl control_;
  signal::Oscillator oscillator_;
  int16_t mono_[kChunkFrames] = {};
  int16_t chunk_[kChunkFrames * 2] = {};
#ifdef DREHKLANG_GENERATOR_DEBUG
  void logGenerator(uint32_t rate);
  uint32_t debugSamples_ = 0;
  uint32_t debugCrossings_ = 0;
  int16_t debugPeak_ = 0;
  int16_t debugLast_ = 0;
#endif
};

}  // namespace drehklang::drivers
