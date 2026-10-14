#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace drehklang::btlink {

// What happened on one baseband link, for the Bluetooth log when it is lost
// (ADR 0027). The headphones sometimes drop the link 30 s after connecting
// with an LMP response timeout (0x22): one side left a link-manager request
// unanswered for the spec's 30 s. The host never sees LMP itself, only the
// steps that complete, so the summary lists which of them were seen, when,
// and which never were -- the request left hanging belongs to one of those.
//
// Pure logic, fed from Bluedroid's callbacks (all on one task) with an
// explicit clock.
class LinkTimeline {
 public:
  enum class Step : uint8_t { Auth, Encryption, A2dp, Audio, Avrcp, Count };

  // A baseband link came up; `ours` when we were connecting to them.
  void aclUp(uint32_t nowMs, bool ours) {
    *this = LinkTimeline{};
    active_ = true;
    upMs_ = nowMs;
    ours_ = ours;
  }

  // The first time only: a later repeat says nothing new.
  void done(Step step, uint32_t nowMs) {
    if (!active_) return;
    uint32_t &at = doneMs_[static_cast<size_t>(step)];
    if (at == kNever) at = nowMs - upMs_;
  }

  // The latest power mode change (0 active, 2 sniff).
  void modeChanged(uint8_t mode, uint32_t nowMs) {
    if (!active_) return;
    mode_ = mode;
    modeMs_ = nowMs - upMs_;
  }

  // Up to three lines, each under 64 characters, handed to `emit`; none
  // without a link. Ends the link's timeline.
  template <typename Emit>
  void summarize(uint32_t nowMs, uint8_t reason, Emit emit) {
    if (!active_) return;
    active_ = false;
    char line[64];
    char lasted[12];
    seconds(nowMs - upMs_, lasted, sizeof(lasted));
    snprintf(line, sizeof(line), "link lost after %s s, reason 0x%02x, %s connected", lasted,
             reason, ours_ ? "we" : "they");
    emit(line);

    Writer seen(line, sizeof(line), "seen: ");
    for (size_t i = 0; i < kSteps; ++i) {
      if (doneMs_[i] == kNever) continue;
      char at[12];
      seconds(doneMs_[i], at, sizeof(at));
      seen.add("%s %s", kNames[i], at);
    }
    if (mode_ != kNoMode) {
      char at[12];
      seconds(modeMs_, at, sizeof(at));
      seen.add("mode %u at %s", static_cast<unsigned>(mode_), at);
    }
    if (seen.empty()) seen.add("nothing");
    emit(line);

    Writer missing(line, sizeof(line), "not seen: ");
    for (size_t i = 0; i < kSteps; ++i) {
      if (doneMs_[i] == kNever) missing.add("%s", kNames[i]);
    }
    if (!missing.empty()) emit(line);
  }

 private:
  static constexpr size_t kSteps = static_cast<size_t>(Step::Count);
  static constexpr uint32_t kNever = UINT32_MAX;
  static constexpr uint8_t kNoMode = 0xFF;
  static constexpr const char *kNames[kSteps] = {"auth", "encryption", "a2dp", "audio",
                                                 "avrcp"};

  // A comma-separated list after a prefix, cut at the buffer's end.
  class Writer {
   public:
    Writer(char *out, size_t size, const char *prefix) : out_(out), size_(size) {
      used_ = static_cast<size_t>(snprintf(out_, size_, "%s", prefix));
      start_ = used_;
    }
    template <typename... Args>
    void add(const char *format, Args... args) {
      if (used_ >= size_) return;
      if (used_ > start_) used_ += static_cast<size_t>(snprintf(out_ + used_, size_ - used_, ", "));
      if (used_ >= size_) return;
      used_ += static_cast<size_t>(snprintf(out_ + used_, size_ - used_, format, args...));
    }
    bool empty() const { return used_ == start_; }

   private:
    char *out_;
    size_t size_;
    size_t used_ = 0;
    size_t start_ = 0;
  };

  // Tenths of a second: "30.2".
  static void seconds(uint32_t ms, char *out, size_t size) {
    snprintf(out, size, "%lu.%lu", static_cast<unsigned long>(ms / 1000),
             static_cast<unsigned long>(ms % 1000 / 100));
  }

  bool active_ = false;
  bool ours_ = false;
  uint32_t upMs_ = 0;
  uint32_t doneMs_[kSteps] = {kNever, kNever, kNever, kNever, kNever};
  uint8_t mode_ = kNoMode;
  uint32_t modeMs_ = 0;
};

}  // namespace drehklang::btlink
