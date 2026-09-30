// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "infra/fixed_str.h"
#include "infra/error.h"

namespace mister::svc {

inline constexpr std::size_t kNumIniOptions = 111;

inline constexpr std::size_t kNumExtOptions = 1;

inline constexpr std::size_t kMaxParseErrors = 4;
inline constexpr std::size_t kParseErrorChars = 128;

enum class OptionType : std::uint8_t {
    U8,
    U16,
    U32,
    I32,
    Str,
    Array,
    I8,
    I16,
    Hex8,
    Hex16,
    Hex32,
    F32,
    Hex32Arr,
    StrArr,
};

struct Option {
    std::string_view name;
    OptionType type;
    std::int64_t min;
    std::int64_t max;
    std::int64_t def;
    std::uint16_t offset;
    std::uint16_t length;

    bool sentinel_default;
};

std::span<const Option> option_schema();

std::span<const Option> option_schema_ext();

enum class SectionMatch : std::uint8_t {
    Global,
    ExactCoreName,
    VideoQualified,
    VideoUnqualified,
    PrefixWildcard,
    NoMatch,
};

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

}  // namespace mister::svc
