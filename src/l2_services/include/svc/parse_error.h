// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

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

struct ParseErrorWording {
    ParseError::Kind kind;
    std::string_view words;
};

inline constexpr ParseErrorWording kParseErrorText[] = {
    {ParseError::Kind::UnknownOption, "unknown option"},
    {ParseError::Kind::NotANumber, "' not a number"},
    {ParseError::Kind::OutOfRange, "' out of range"},
    {ParseError::Kind::InvalidFormat, "' invalid format"},
};

[[nodiscard]] constexpr std::string_view wording_for(ParseError::Kind kind) noexcept {
    for (const ParseErrorWording& row : kParseErrorText) {
        if (row.kind == kind)
            return row.words;
    }
    return {};
}

consteval bool error_texts_cover_every_kind() {
    using K = ParseError::Kind;
    return !wording_for(K::UnknownOption).empty()
        && !wording_for(K::NotANumber).empty()
        && !wording_for(K::OutOfRange).empty()
        && !wording_for(K::InvalidFormat).empty();
}

static_assert(error_texts_cover_every_kind(), "kParseErrorText: one wording per ParseError::Kind");

}
