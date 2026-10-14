#pragma once

#include "Messages.h"
#include "PeerList.h"

namespace drehklang::bt {

// The remembered headphones (btlink::PeerList), in this chip's own NVS.
// The bonding keys are Bluedroid's own business; this keeps whom to try,
// in which order. Change the list through list(), then save().
class PeerStore {
 public:
  void begin();
  btlink::PeerList &list() { return list_; }
  const btlink::PeerList &list() const { return list_; }
  void save();

 private:
  btlink::PeerList list_;
};

}  // namespace drehklang::bt
