// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "svc/xml_attr.h"

namespace mister::svc::xml {

struct Tag {
    std::string_view name;
    bool closing = false;
    bool self_closing = false;

    std::span<const Attr> attrs{};

    std::uint8_t attrs_dropped = 0;
};

}
