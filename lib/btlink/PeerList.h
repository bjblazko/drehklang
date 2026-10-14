#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Messages.h"

namespace drehklang::btlink {

// The headphones the U4WDH remembers (ADR 0027), the most recently used
// first, at most kMaxPeers. Bluedroid keeps the bonding keys; this keeps
// whom to try, in which order, and their names. Pure, so it is host-tested;
// bt/src/PeerStore.cpp keeps it in NVS.
class PeerList {
 public:
  const std::vector<Peer> &peers() const { return peers_; }
  bool empty() const { return peers_.empty(); }

  int indexOf(const Address &address) const {
    for (size_t i = 0; i < peers_.size(); ++i) {
      if (peers_[i].address == address) return static_cast<int>(i);
    }
    return -1;
  }

  // To the front, with this name. Returns the peer that fell off the end
  // when the list was full: its bond has to go as well.
  std::optional<Peer> add(const Address &address, const std::string &name) {
    remove(address);
    peers_.insert(peers_.begin(), Peer{address, name});
    if (peers_.size() <= kMaxPeers) return std::nullopt;
    Peer evicted = peers_.back();
    peers_.pop_back();
    return evicted;
  }

  // A known peer to the front. True when the order changed.
  bool touch(const Address &address) {
    const int at = indexOf(address);
    if (at <= 0) return false;
    std::rotate(peers_.begin(), peers_.begin() + at, peers_.begin() + at + 1);
    return true;
  }

  bool remove(const Address &address) {
    const int at = indexOf(address);
    if (at < 0) return false;
    peers_.erase(peers_.begin() + at);
    return true;
  }

  // Count u8, then per peer the address and the name (length u8, bytes).
  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> out{static_cast<uint8_t>(peers_.size())};
    for (const auto &peer : peers_) {
      out.insert(out.end(), peer.address.begin(), peer.address.end());
      const size_t n = utf8Prefix(peer.name, kMaxNameBytes);
      out.push_back(static_cast<uint8_t>(n));
      out.insert(out.end(), peer.name.begin(), peer.name.begin() + static_cast<long>(n));
    }
    return out;
  }

  // False, and an empty list, for anything malformed.
  bool deserialize(const uint8_t *bytes, size_t length) {
    peers_.clear();
    if (length < 1 || bytes[0] > kMaxPeers) return false;
    size_t at = 1;
    std::vector<Peer> read(bytes[0]);
    for (auto &peer : read) {
      if (at + 7 > length) return false;
      std::copy(bytes + at, bytes + at + 6, peer.address.begin());
      at += 6;
      const size_t n = bytes[at++];
      if (n > kMaxNameBytes || at + n > length) return false;
      peer.name.assign(reinterpret_cast<const char *>(bytes + at), n);
      at += n;
    }
    if (at != length) return false;
    peers_ = std::move(read);
    return true;
  }

 private:
  std::vector<Peer> peers_;
};

}  // namespace drehklang::btlink
