// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "infra/seat.h"
#include "svc/input/device_identity.h"
#include "svc/input/quirk_id.h"

namespace mister::svc::input {

struct DeviceFacts {
    TASTY_SEAT_EXEMPT(component);
    std::string devname;
    bool mouse = false;
    std::string uniq;

    std::string mac;
    std::string sysfs;
    DeviceIdentity id{};
    QuirkId quirk = QuirkId::None;
    std::uint8_t spinner_prediv = 0;
    unsigned bind = 0;

    void inherit_identity_from(const DeviceFacts& src);
};

}
