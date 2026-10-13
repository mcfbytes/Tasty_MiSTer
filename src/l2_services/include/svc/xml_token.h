// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "svc/xml_tag.h"

namespace mister::svc::xml {

struct Token {
    enum class Kind : std::uint8_t { Text, Start, End, Markup };
    Kind kind = Kind::Text;
    std::string_view text;
    Tag tag;
};

}  // namespace mister::svc::xml
