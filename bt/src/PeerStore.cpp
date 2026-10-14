#include "PeerStore.h"

#include <Preferences.h>

#include <vector>

namespace drehklang::bt {

namespace {
constexpr char kNamespace[] = "btpeer";
constexpr char kListKey[] = "peers";
}  // namespace

void PeerStore::begin() {
  Preferences prefs;
  // Read-write even to read: a read-only open of a namespace that does not
  // exist yet (first boot) logs an error.
  prefs.begin(kNamespace, false);
  // isKey() first: Preferences logs an error for every key it cannot find.
  if (prefs.isKey(kListKey)) {
    std::vector<uint8_t> bytes(prefs.getBytesLength(kListKey));
    prefs.getBytes(kListKey, bytes.data(), bytes.size());
    list_.deserialize(bytes.data(), bytes.size());
  } else if (prefs.isKey("addr")) {
    // Protocol 1 kept one pair as "addr" and "name": it becomes the list.
    btlink::Address address{};
    if (prefs.getBytes("addr", address.data(), address.size()) == address.size()) {
      list_.add(address, prefs.isKey("name") ? prefs.getString("name", "").c_str() : "");
    }
    prefs.remove("addr");
    if (prefs.isKey("name")) prefs.remove("name");
    const std::vector<uint8_t> bytes = list_.serialize();
    prefs.putBytes(kListKey, bytes.data(), bytes.size());
  }
  prefs.end();
}

void PeerStore::save() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  const std::vector<uint8_t> bytes = list_.serialize();
  prefs.putBytes(kListKey, bytes.data(), bytes.size());
  prefs.end();
}

}  // namespace drehklang::bt
