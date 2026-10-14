#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "Messages.h"
#include "PeerList.h"

namespace drehklang::btlink {

// Whom the U4WDH tries to connect to next, and how long to wait after that
// attempt (ADR 0027). Remembered headphones are tried in turn, the most
// recently used first; after a full round it pauses. Headphones the user
// picked (from the search, or by tapping their row) are tried alone for a
// minute, so the others do not get in the way.
class ConnectPlan {
 public:
  // Between two peers in a round. An attempt on absent headphones takes
  // ~5 s itself (page timeout).
  static constexpr uint32_t kBetweenPeersMs = 3000;
  // After a round, as the single peer was retried before.
  static constexpr uint32_t kRoundPauseMs = 10000;
  static constexpr uint32_t kWantedForMs = 60000;

  struct Attempt {
    Address address{};
    uint32_t pauseMs = 0;
  };

  std::optional<Attempt> next(const PeerList &list, uint32_t nowMs) {
    if (const auto picked = wanted(nowMs)) return Attempt{*picked, kBetweenPeersMs};
    if (list.empty()) return std::nullopt;
    if (turn_ >= list.peers().size()) turn_ = 0;
    Attempt attempt{list.peers()[turn_].address, kBetweenPeersMs};
    if (++turn_ >= list.peers().size()) {
      turn_ = 0;
      attempt.pauseMs = kRoundPauseMs;
    }
    return attempt;
  }

  void want(const Address &address, uint32_t nowMs) {
    wanted_ = address;
    wantedSinceMs_ = nowMs;
  }

  std::optional<Address> wanted(uint32_t nowMs) const {
    if (!wanted_ || nowMs - wantedSinceMs_ >= kWantedForMs) return std::nullopt;
    return wanted_;
  }

  void forget(const Address &address) {
    if (wanted_ == address) wanted_.reset();
  }

  // Connected to someone: the wish is over, and the next round starts at
  // the front.
  void connected() {
    wanted_.reset();
    turn_ = 0;
  }

 private:
  std::optional<Address> wanted_;
  uint32_t wantedSinceMs_ = 0;
  size_t turn_ = 0;
};

}  // namespace drehklang::btlink
