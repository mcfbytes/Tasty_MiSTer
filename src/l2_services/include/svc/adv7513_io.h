// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc::adv7513 {

class IRegMap;

inline constexpr std::uint8_t kMainAddr = 0x39;
inline constexpr std::uint8_t kCecAddr = 0x3C;
inline constexpr std::uint8_t kEdidAddr = 0x3F;
inline constexpr std::uint8_t kSpdAddr = 0x38;

}  // namespace mister::svc::adv7513

namespace mister::hal {
class IHdmiInterrupt;
}
namespace mister::os {
class IDelay;
class IClock;
}  // namespace mister::os

namespace mister::svc::adv7513 {

struct Io {
    IRegMap* main = nullptr;
    IRegMap* edid = nullptr;
    IRegMap* cec = nullptr;
    const hal::IHdmiInterrupt* hdmi = nullptr;

    os::IDelay* delay = nullptr;
    const os::IClock* clock = nullptr;
};

}  // namespace mister::svc::adv7513
