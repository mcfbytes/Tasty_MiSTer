// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "infra/fixed_str.h"

namespace mister::cores::n64 {

enum class ByteOrder : std::uint8_t {
    Big = 0,
    ByteSwapped,
    Little,
    Unknown,
};

inline constexpr std::size_t kMinRomBytes = 4096;

inline constexpr std::size_t kHeaderBytes = 0x40;

inline constexpr std::size_t kInternalNameOffset = 0x20;
inline constexpr std::size_t kCartIdOffset = 0x3b;
inline constexpr std::size_t kCartIdRevisionOffset = 0x3f;

inline constexpr std::size_t kCartIdChars = 6;
using CartId = FixedStr<kCartIdChars + 1, StrFit::Reject>;

inline constexpr std::size_t kInternalNameChars = 20;
using InternalName = FixedStr<kInternalNameChars + 1, StrFit::Reject>;

namespace detail {

[[nodiscard]] constexpr bool ascii_alnum(std::uint8_t c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

[[nodiscard]] constexpr bool ascii_space(std::uint8_t c) noexcept {
    return c == ' ' || (c >= 0x09 && c <= 0x0d);
}

[[nodiscard]] constexpr char hex_digit(std::uint8_t nibble) noexcept {
    return static_cast<char>(nibble < 10 ? '0' + nibble : 'A' + (nibble - 10));
}

}

[[nodiscard]] constexpr ByteOrder detect_byte_order(std::span<const std::uint8_t> chunk) noexcept {
    if (chunk.size() < 4)
        return ByteOrder::Unknown;

    const std::uint32_t val = static_cast<std::uint32_t>(chunk[0])
        | (static_cast<std::uint32_t>(chunk[1]) << 8)
        | (static_cast<std::uint32_t>(chunk[2]) << 16)
        | (static_cast<std::uint32_t>(chunk[3]) << 24);

    switch (val) {
        case UINT32_C(0x40123780):
        case UINT32_C(0x40072780):
        case UINT32_C(0x41123780):
            return ByteOrder::Big;
        case UINT32_C(0x12408037):
        case UINT32_C(0x07408027):
        case UINT32_C(0x12418037):
            return ByteOrder::ByteSwapped;
        case UINT32_C(0x80371240):
        case UINT32_C(0x80270740):
        case UINT32_C(0x80371241):
            return ByteOrder::Little;
        default:
            break;
    }

    switch (val & 0xffu) {
        case 0x80:
            return ByteOrder::Big;
        case 0x37:
        case 0x27:
            return ByteOrder::ByteSwapped;
        case 0x40:
        case 0x41:
            return ByteOrder::Little;
        default:
            return ByteOrder::Unknown;
    }
}

constexpr void normalize(std::span<std::uint8_t> data, ByteOrder order) noexcept {
    const std::size_t size = data.size();
    switch (order) {
        case ByteOrder::ByteSwapped:
            for (std::size_t i = 0; i + 2 <= size; i += 2) {
                std::swap(data[i], data[i + 1]);
            }
            break;
        case ByteOrder::Little:
            for (std::size_t i = 0; i + 4 <= size; i += 4) {
                std::swap(data[i], data[i + 3]);
                std::swap(data[i + 1], data[i + 2]);
            }
            break;
        case ByteOrder::Big:
        case ByteOrder::Unknown:
            break;
    }
}

[[nodiscard]] constexpr CartId cart_id(std::span<const std::uint8_t> header) noexcept {
    CartId out;
    if (header.size() < kHeaderBytes)
        return out;

    std::array<char, kCartIdChars> buf{};
    bool any_named = false;
    for (std::size_t i = 0; i < 4; ++i) {
        const std::uint8_t c = header[kCartIdOffset + i];
        buf[i] = detail::ascii_alnum(c) ? static_cast<char>(c) : '?';
        any_named = any_named || buf[i] != '?';
    }
    if (!any_named)
        return out;

    const std::uint8_t rev = header[kCartIdRevisionOffset];
    buf[4] = detail::hex_digit(static_cast<std::uint8_t>(rev >> 4));
    buf[5] = detail::hex_digit(static_cast<std::uint8_t>(rev & 0x0fu));
    (void)out.assign(std::string_view{buf.data(), buf.size()});
    return out;
}

[[nodiscard]] constexpr InternalName internal_name(std::span<const std::uint8_t> header) noexcept {
    InternalName out;
    if (header.size() < kHeaderBytes)
        return out;

    const std::span<const std::uint8_t> field = header.subspan(
        kInternalNameOffset,
        kInternalNameChars + 1
    );
    std::size_t begin = 0;
    std::size_t budget = kInternalNameChars + 1;
    if (field[0] == 0)
        return out;
    while (detail::ascii_space(field[begin])) {
        if (--budget == 0)
            return out;
        ++begin;
    }
    if (field[begin] == 0)
        return out;

    std::size_t len = 0;
    while (len < budget && field[begin + len] != 0)
        ++len;

    while (len > 1 && detail::ascii_space(field[begin + len - 1]))
        --len;
    len = std::min(len, kInternalNameChars);

    std::array<char, kInternalNameChars> buf{};
    for (std::size_t i = 0; i < len; ++i) {
        const std::uint8_t c = field[begin + i];
        buf[i] = (c >= 0x20 && c < 0xa0) ? static_cast<char>(c) : '?';
    }
    (void)out.assign(std::string_view{buf.data(), len});
    return out;
}

}
