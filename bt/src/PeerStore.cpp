#include "PeerStore.h"

#include <Preferences.h>

namespace drehklang::bt {

namespace {
constexpr char kNamespace[] = "btpeer";
}

void PeerStore::begin() {
  Preferences prefs;
  // Read-write even to read: a read-only open of a namespace that does not
  // exist yet (first boot) logs an error.
  prefs.begin(kNamespace, false);
  // isKey() first: Preferences logs an error for every key it cannot find.
  has_ = prefs.isKey("addr") &&
         prefs.getBytes("addr", address_.data(), address_.size()) == address_.size();
  if (prefs.isKey("name")) name_ = prefs.getString("name", "").c_str();
  prefs.end();
}

void PeerStore::save(const btlink::Address &address, const std::string &name) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putBytes("addr", address.data(), address.size());
  prefs.putString("name", name.c_str());
  prefs.end();
  has_ = true;
  address_ = address;
  name_ = name;
}

void PeerStore::clear() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.clear();
  prefs.end();
  has_ = false;
  address_ = {};
  name_.clear();
}

}  // namespace drehklang::bt
