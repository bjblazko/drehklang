#pragma once

namespace drehklang {

// Shown in About and logged at boot by both chips. Set by hand: there are
// no binary releases (ADR 0027), so this names the source state a device
// was built from. Bump it for every build that goes onto the device:
// patch for fixes only, minor when a feature came in, each its own
// "Release x.y.z: ..." commit (AGENTS.md).
inline constexpr const char *kVersion = "2.6.2";

}  // namespace drehklang
