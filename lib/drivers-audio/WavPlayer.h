#pragma once

#include <FS.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "DacArbiter.h"
#include "DictaphonePorts.h"
#include "WavHeader.h"

namespace drehklang::drivers {

// Plays a dictaphone recording (ADR 0032): 16-bit mono PCM WAV, read
// straight from the card -- 32 KB/s at 16 kHz needs no decoder. Not the
// music player, whose play() would replace the queue and the resume record
// with a recording.
//
// Like ToneOutput: an owner of I2S0 with its own channel, its own task on
// core 0 doing the blocking writes, the volume from AudioGain, every
// sample noted for the band and copied to the Bluetooth tap.
class WavPlayer : public dictaphone::RecordingPlayer, public playback::DacOwner {
 public:
  explicit WavPlayer(playback::DacArbiter &dac) : dac_(dac) {}

  // Starts the task. Call once.
  void begin();

  // dictaphone::RecordingPlayer -- main loop only.
  bool open(const std::string &path) override;
  void play() override;
  void pause() override { playing_.store(false); }
  void close() override;
  bool playing() const override { return playing_.load(); }
  uint32_t positionMs() const override { return bytesToMs(position_.load()); }
  uint32_t durationMs() const override { return info_.durationMs(); }
  void rewind(uint32_t ms) override;
  bool remove(const std::string &path) override;

  // signal::SampleSource -- what was last played, for the band.
  size_t readRecent(int16_t *dst, size_t maxSamples) override;

  // playback::DacOwner -- someone else wants I2S0.
  void releaseDac() override;

 private:
  static constexpr size_t kChunkFrames = 256;

  uint32_t bytesToMs(uint32_t bytes) const {
    const uint32_t rate = info_.format.byteRate();
    return rate ? static_cast<uint32_t>(static_cast<uint64_t>(bytes) * 1000 / rate) : 0;
  }
  bool openChannel();
  void closeChannel();
  // One chunk from the file to the channel; false at the end of the data
  // or when there is no channel.
  bool playChunk();
  [[noreturn]] void taskLoop();
  static void taskTrampoline(void *self);

  playback::DacArbiter &dac_;
  // Guards file_, channel_ and info_ between the main loop and the task.
  SemaphoreHandle_t mutex_ = nullptr;
  i2s_chan_handle_t channel_ = nullptr;
  fs::File file_;
  std::string path_;
  dictaphone::WavInfo info_;
  // Bytes of PCM data played, from the start of the data chunk.
  std::atomic<uint32_t> position_{0};
  std::atomic<bool> playing_{false};
  int16_t mono_[kChunkFrames] = {};
  int16_t stereo_[kChunkFrames * 2] = {};
};

}  // namespace drehklang::drivers
