#pragma once

#include <string>

#include "Messages.h"

namespace drehklang::bt {

// The one paired pair of headphones, in this chip's own NVS. The bonding
// keys are Bluedroid's own business; this keeps whom to reconnect to.
class PeerStore {
 public:
  void begin();
  bool has() const { return has_; }
  const btlink::Address &address() const { return address_; }
  const std::string &name() const { return name_; }
  void save(const btlink::Address &address, const std::string &name);
  void clear();

 private:
  bool has_ = false;
  btlink::Address address_{};
  std::string name_;
};

}  // namespace drehklang::bt
