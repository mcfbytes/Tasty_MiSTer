// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/fixed_str.h"

namespace mister::svc {

inline constexpr std::size_t kParseErrorChars = 128;

struct ParseError {
    enum class Kind : std::uint8_t {
        UnknownOption,
        NotANumber,
        OutOfRange,
        InvalidFormat,
    };
    Kind kind{Kind::UnknownOption};
    FixedStr<kParseErrorChars, StrFit::Clip> text{};
};

}
