// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/types.h"

namespace mister::svc::adv7513 {

class IRegMap;

inline constexpr os::I2cAddr kMainAddr{0x39};
inline constexpr os::I2cAddr kCecAddr{0x3C};
inline constexpr os::I2cAddr kEdidAddr{0x3F};
inline constexpr os::I2cAddr kSpdAddr{0x38};

}

namespace mister::hal {
class IHdmiInterrupt;
}

namespace mister::os {
class IDelay;
class IClock;
}

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

}
