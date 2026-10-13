// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/opt_ref.h"
#include "infra/seat.h"

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
    TASTY_SEAT_RESIDENT(Ui);
    infra::OptRef<IRegMap> main{};
    infra::OptRef<IRegMap> edid{};
    infra::OptRef<IRegMap> cec{};
    infra::OptRef<const hal::IHdmiInterrupt> hdmi{};

    infra::OptRef<os::IDelay> delay{};
    infra::OptRef<const os::IClock> clock{};
};

inline constexpr Io kUnboundIo{};

}  // namespace mister::svc::adv7513
