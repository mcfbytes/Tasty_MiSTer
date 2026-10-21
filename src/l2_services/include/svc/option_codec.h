// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

#include "svc/config.h"

namespace mister::svc {

enum class ParseDiag : std::uint8_t { None, NotANumber, OutOfRange, InvalidFormat };

struct ParsedValue {
    std::uint32_t bits = 0;
    ParseDiag diag = ParseDiag::None;
};

enum class FieldShape : std::uint8_t {
    Inert,
    Scalar,
    String,
    StringSlots,
    CountedU32,
};

ParsedValue parse_unsigned(const Option& o, const char* text);
ParsedValue parse_hex(const Option& o, const char* text);
ParsedValue parse_signed(const Option& o, const char* text);
ParsedValue parse_float(const Option& o, const char* text);

void store_u8(std::byte* out, std::uint32_t bits);
void store_i8(std::byte* out, std::uint32_t bits);
void store_u16(std::byte* out, std::uint32_t bits);
void store_i16(std::byte* out, std::uint32_t bits);
void store_u32(std::byte* out, std::uint32_t bits);
void store_i32(std::byte* out, std::uint32_t bits);
void store_f32(std::byte* out, std::uint32_t bits);

struct OptionCodec {
    OptionType type;
    std::size_t width;
    FieldShape shape;
    ParsedValue (*parse)(const Option&, const char*);
    void (*store)(std::byte*, std::uint32_t);
};

inline constexpr OptionCodec kOptionCodecs[] = {
    {OptionType::U8, 1, FieldShape::Scalar, parse_unsigned, store_u8},
    {OptionType::U16, 2, FieldShape::Scalar, parse_unsigned, store_u16},
    {OptionType::U32, 4, FieldShape::Scalar, parse_unsigned, store_u32},
    {OptionType::I32, 4, FieldShape::Scalar, parse_signed, store_i32},
    {OptionType::Str, 0, FieldShape::String, nullptr, nullptr},
    {OptionType::Array, 0, FieldShape::CountedU32, parse_unsigned, store_u32},
    {OptionType::I8, 1, FieldShape::Scalar, parse_signed, store_i8},
    {OptionType::I16, 2, FieldShape::Scalar, parse_signed, store_i16},
    {OptionType::Hex8, 1, FieldShape::Scalar, parse_hex, store_u8},
    {OptionType::Hex16, 2, FieldShape::Scalar, parse_hex, store_u16},
    {OptionType::Hex32, 4, FieldShape::Scalar, parse_hex, store_u32},
    {OptionType::F32, 4, FieldShape::Scalar, parse_float, store_f32},
    {OptionType::Hex32Arr, 0, FieldShape::CountedU32, parse_hex, store_u32},
    {OptionType::StrArr, 0, FieldShape::StringSlots, nullptr, nullptr},
};

consteval bool codecs_in_enum_order() {
    for (std::size_t i = 0; i < std::size(kOptionCodecs); ++i) {
        if (static_cast<std::size_t>(kOptionCodecs[i].type) != i)
            return false;
    }
    return true;
}

static_assert(codecs_in_enum_order(), "kOptionCodecs must list OptionType in enumerator order");
static_assert(
    std::size(kOptionCodecs) == static_cast<std::size_t>(OptionType::StrArr) + 1,
    "every OptionType needs a codec row"
);

inline constexpr OptionCodec kInertCodec{OptionType::U8, 0, FieldShape::Inert, nullptr, nullptr};

[[nodiscard]] constexpr const OptionCodec& codec_for(OptionType t) noexcept {
    const auto i = static_cast<std::size_t>(t);
    return i < std::size(kOptionCodecs) ? kOptionCodecs[i] : kInertCodec;
}

[[nodiscard]] std::int64_t clamp_to_row(const Option& o, std::int64_t v) noexcept;

}
