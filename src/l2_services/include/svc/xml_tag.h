// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace mister::svc::xml {

struct Tag {
    std::string_view name;
    bool closing = false;
    bool self_closing = false;

    std::uint8_t attr_count = 0;

    std::uint8_t attrs_dropped = 0;
};

}  // namespace mister::svc::xml
