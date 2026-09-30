// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"

namespace mister::os {

struct NetPresence {
    bool wired = false;
    bool wifi = false;
};

[[nodiscard]] Ex<NetPresence> net_presence();

struct Ipv4If {
    std::string_view name;
    std::uint8_t octet0 = 0;
    std::uint8_t octet1 = 0;
};

NetPresence net_presence_from_list(std::span<const Ipv4If> ifs) noexcept;

}  // namespace mister::os
