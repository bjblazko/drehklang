#pragma once

#include <cstdint>
#include <string>

#include "SampleSource.h"

namespace drehklang::dictaphone {

// The dictaphone's two halves as the screens see them (ADR 0032), so the
// UI layer knows nothing about I2S, PDM or the SD card. Both are sample
// sources for the band: what the microphone hears, or what is playing.
// Main loop only.

// The microphone, and recording what it hears to a WAV file.
class Recorder : public signal::SampleSource {
 public:
  // Microphone on: claims I2S0 from the player or the tone output. False
  // when the channel could not be opened.
  virtual bool listen() = 0;
  // Microphone off; a running recording is stopped and saved first.
  virtual void close() = 0;
  // Starts writing to `path` (its folder is created). False when the file
  // could not be created or the microphone is off.
  virtual bool record(const std::string &path) = 0;
  // Stops writing; the file is finished on the writer's task shortly after.
  virtual void stopRecording() = 0;
  // Still true while paused.
  virtual bool recording() const = 0;
  // Holds a running recording without closing its file: nothing is
  // written and the time stands still until resumeRecording().
  virtual void pauseRecording() = 0;
  virtual void resumeRecording() = 0;
  virtual bool paused() const = 0;
  // True while a recording's file is still open, which includes the short
  // while after stopRecording() until it is finished: the card is not to
  // be handed to a computer then.
  virtual bool saving() const = 0;
  // Time recorded so far, or of the last recording once stopped.
  virtual uint32_t recordedMs() const = 0;
  // True once after a recording stopped by itself because a write failed
  // (card full or pulled). What reached the card is kept.
  virtual bool takeWriteFailure() = 0;
};

// Plays one recording at a time on its own I2S0 channel.
class RecordingPlayer : public signal::SampleSource {
 public:
  // Reads the header; false when the file is missing or not PCM WAV.
  virtual bool open(const std::string &path) = 0;
  // Claims I2S0. From the start again after the end was reached.
  virtual void play() = 0;
  virtual void pause() = 0;
  // Stops and forgets the file.
  virtual void close() = 0;
  virtual bool playing() const = 0;
  virtual uint32_t positionMs() const = 0;
  virtual uint32_t durationMs() const = 0;
  // Jumps back by `ms`, no further than the start.
  virtual void rewind(uint32_t ms) = 0;
  // Closes the file if it is open, then deletes it.
  virtual bool remove(const std::string &path) = 0;
};

}  // namespace drehklang::dictaphone
