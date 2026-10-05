#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "DacArbiter.h"
#include "PlaybackDriver.h"

class Audio;

namespace drehklang::drivers {

// I2S pins for the PCM5100A DAC, per device.md's pinout.
constexpr uint8_t kAudioBclkPin = 39;
constexpr uint8_t kAudioLrcPin = 40;
constexpr uint8_t kAudioDoutPin = 41;

// Wraps ESP32-audioI2S's Audio class behind PlaybackDriver -- the only
// file including <Audio.h> (ADR 0001, coding-guidelines.md
// hardware/logic separation). One decoder library for every format
// Drehklang plays (ADR 0026).
//
// The library decodes on its own task, but its loop() still has to be
// called often to keep the input buffer filled from SD. That runs on this
// driver's own task on core 0, away from LVGL's blocking display flush on
// core 1, which starved it into audible stutter when both shared loop()
// (ADR 0006). mutex_ serialises that task against the main loop's calls.
//
// The DAC has one owner at a time (ADR 0026): Audio -- and with it the
// I2S channel -- exists only while the player owns it. When the tone
// output takes the DAC, the track is parked (path and heard position
// kept) and reopened there on the next resume().
class Esp32AudioI2SDriver : public playback::PlaybackDriver, public playback::DacOwner {
 public:
  explicit Esp32AudioI2SDriver(playback::DacArbiter &dac);
  ~Esp32AudioI2SDriver() override;

  // Starts the loop task.
  void begin();

  bool playFile(const std::string &path) override { return playFileAt(path, 0); }
  bool playFileAt(const std::string &path, uint32_t position) override;
  uint32_t filePosition() override;
  bool seekByMs(int32_t deltaMs) override;
  void pause() override;
  void resume() override;
  void stop() override;
  void setVolume(uint8_t volume) override;
  // Lock-free: read per sample on the decode task (audio_process_i2s()).
  void setOutputGain(uint16_t gain) override;
  bool isRunning() override;
  uint32_t durationSeconds() override;
  playback::SampleWindow readRecentSamples(int16_t *dst, size_t maxSamples) override;
  // No-op: the loop task services the library continuously.
  void loop() override {}

  // playback::DacOwner -- the tone output wants the DAC.
  void releaseDac() override;

 private:
  // What the decoder's own estimates get wrong, read from the file itself.
  struct TrackTiming {
    uint32_t durationSeconds = 0;  // 0 when unknown.
    uint32_t dataStart = 0;        // Audio data byte range; both 0 when
    uint32_t dataEnd = 0;          // the library's own values are right.
  };

  static TrackTiming readTrackTiming(const std::string &path);
  bool openTrack(const std::string &path, uint32_t position);
  void ensureAudio();
  uint32_t heardPosition();
  void applyPendingSeek();
  [[noreturn]] void taskLoop();
  static void taskTrampoline(void *self);

  // Audio must sit in internal RAM: it is the I2S driver's callback
  // context, and the core is built with CONFIG_I2S_ISR_IRAM_SAFE, which
  // rejects one in PSRAM -- where a plain heap allocation lands here
  // (heap_caps_malloc_extmem_enable). The library aborts on that
  // (2026-10-05). The deleter frees what ensureAudio() allocated.
  struct InternalRamDeleter {
    void operator()(Audio *audio) const;
  };

  playback::DacArbiter &dac_;
  std::unique_ptr<Audio, InternalRamDeleter> audio_;
  SemaphoreHandle_t mutex_ = nullptr;

  std::string path_;  // Current track; empty when stopped.
  TrackTiming timing_;
  bool paused_ = false;
  bool parked_ = false;  // Track kept, DAC handed to the tone output.
  uint32_t parkedPosition_ = 0;
  // A resume position waiting for the library to parse the file's header;
  // -1 when none.
  int64_t pendingSeek_ = -1;
  uint8_t volume_ = 0;
  // Published by the loop task for the spectrum, which reads it lock-free.
  std::atomic<uint32_t> sampleRate_{44100};
};

}  // namespace drehklang::drivers
