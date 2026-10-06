#pragma once

namespace drehklang {

// Shown in About and logged at boot. Set by hand: there are no binary
// releases (ADR 0027), so this names the source state a device was built
// from. Bump it when a change is worth telling apart on a device.
inline constexpr const char *kVersion = "2.1.0";

}  // namespace drehklang
