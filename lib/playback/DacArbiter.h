#pragma once

namespace drehklang::playback {

// Something that can drive the DAC: the music player or the tone output
// (ADR 0026). Each opens its own I2S channel while it owns the DAC and
// closes it in releaseDac(), so the two never share a port.
class DacOwner {
 public:
  virtual ~DacOwner() = default;
  // Close the I2S channel. The player keeps its track and position and
  // reopens it on the next resume.
  virtual void releaseDac() = 0;
};

// One owner of the DAC at a time. Tones and games never play over music,
// so a handover beats sharing one I2S port between two writers.
//
// Main loop only: every claim comes from a user action (play, a blip, the
// tone generator's start), so there is no locking here. Each owner guards
// its own channel against its own audio task.
class DacArbiter {
 public:
  // Makes `owner` the owner, releasing the previous one first.
  void claim(DacOwner &owner) {
    if (owner_ == &owner) return;
    if (owner_ != nullptr) owner_->releaseDac();
    owner_ = &owner;
  }

  bool owns(const DacOwner &owner) const { return owner_ == &owner; }

 private:
  DacOwner *owner_ = nullptr;
};

}  // namespace drehklang::playback
