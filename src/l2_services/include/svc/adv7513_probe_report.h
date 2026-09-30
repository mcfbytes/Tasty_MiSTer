// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc::adv7513 {

enum class Presence : std::uint8_t {
    NotProbed = 0,
    Absent,
    Ambiguous,
    Rejected,
    Found,
};

struct ProbeReport {
    Presence presence = Presence::NotProbed;

    std::uint8_t bus = 0xFF;
    std::uint8_t acks = 0;
    std::uint8_t r41 = 0;
    std::uint8_t r42 = 0;
    bool edid_ack = false;
    bool spd_ack = false;
    bool cec_ack = false;
    std::uint32_t last_errno = 0;
};

}  // namespace mister::svc::adv7513
