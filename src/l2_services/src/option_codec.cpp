// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/option_codec.h"

#include <bit>
#include <cstdlib>
#include <span>

#include "infra/endian.h"

namespace mister::svc {

namespace {

bool has_hex_prefix(const char* text) {
    return text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
}

ParseDiag diagnose(const char* end, bool out_of_range, bool invalid_format) {
    if (*end != '\0')
        return ParseDiag::NotANumber;
    if (out_of_range)
        return ParseDiag::OutOfRange;
    if (invalid_format)
        return ParseDiag::InvalidFormat;
    return ParseDiag::None;
}

ParsedValue parse_unsigned_row(const Option& o, const char* text, bool invalid_format) {
    char* end = nullptr;

    const unsigned long long raw = std::strtoull(text, &end, 0);
    const std::int64_t before = static_cast<std::int64_t>(static_cast<std::uint32_t>(raw));
    const std::int64_t after = clamp_to_row(o, before);
    return {static_cast<std::uint32_t>(after), diagnose(end, after != before, invalid_format)};
}

template <class T>
void store_as(std::byte* out, T v) {
    infra::store_native<T>(std::span<std::byte, sizeof(T)>(out, sizeof(T)), v);
}

}

std::int64_t clamp_to_row(const Option& o, std::int64_t v) noexcept {
    if (v < o.min)
        return o.min;
    if (v > o.max)
        return o.max;
    return v;
}

ParsedValue parse_unsigned(const Option& o, const char* text) {
    return parse_unsigned_row(o, text, false);
}

ParsedValue parse_hex(const Option& o, const char* text) {
    return parse_unsigned_row(o, text, !has_hex_prefix(text));
}

ParsedValue parse_signed(const Option& o, const char* text) {
    char* end = nullptr;
    const long long raw = std::strtoll(text, &end, 0);
    const std::int32_t i32 = static_cast<std::int32_t>(raw);
    const std::int64_t before = i32;
    const std::int64_t after = clamp_to_row(o, before);
    return {
        static_cast<std::uint32_t>(static_cast<std::int32_t>(after)),
        diagnose(end, after != before, false)
    };
}

ParsedValue parse_float(const Option& o, const char* text) {
    char* end = nullptr;
    float f32 = std::strtof(text, &end);
    const float lo = static_cast<float>(o.min);
    const float hi = static_cast<float>(o.max);
    bool out_of_range = true;
    if (f32 < lo)
        f32 = lo;
    else if (f32 > hi)
        f32 = hi;
    else
        out_of_range = false;
    return {std::bit_cast<std::uint32_t>(f32), diagnose(end, out_of_range, false)};
}

void store_u8(std::byte* out, std::uint32_t bits) {
    store_as<std::uint8_t>(out, static_cast<std::uint8_t>(bits));
}

void store_i8(std::byte* out, std::uint32_t bits) {
    store_as<std::int8_t>(out, static_cast<std::int8_t>(bits));
}

void store_u16(std::byte* out, std::uint32_t bits) {
    store_as<std::uint16_t>(out, static_cast<std::uint16_t>(bits));
}

void store_i16(std::byte* out, std::uint32_t bits) {
    store_as<std::int16_t>(out, static_cast<std::int16_t>(bits));
}

void store_u32(std::byte* out, std::uint32_t bits) {
    store_as<std::uint32_t>(out, bits);
}

void store_i32(std::byte* out, std::uint32_t bits) {
    store_as<std::int32_t>(out, static_cast<std::int32_t>(bits));
}

void store_f32(std::byte* out, std::uint32_t bits) {
    store_as<float>(out, std::bit_cast<float>(bits));
}

}
