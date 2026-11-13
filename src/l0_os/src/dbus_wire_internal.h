// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "os/types.h"

namespace mister::os::detail {

inline constexpr char kByte = 'y';
inline constexpr char kBoolean = 'b';
inline constexpr char kInt16 = 'n';
inline constexpr char kUint16 = 'q';
inline constexpr char kInt32 = 'i';
inline constexpr char kUint32 = 'u';
inline constexpr char kInt64 = 'x';
inline constexpr char kUint64 = 't';
inline constexpr char kString = 's';
inline constexpr char kObjectPath = 'o';
inline constexpr char kSignature = 'g';
inline constexpr char kVariant = 'v';
inline constexpr char kArray = 'a';
inline constexpr char kDictOpen = '{';
inline constexpr char kDictClose = '}';

inline constexpr WireU8 kLittleEndianMarker{0x6c};
inline constexpr WireU8 kBigEndianMarker{0x42};

[[nodiscard]] constexpr WireBytes align_of(char code) noexcept {
    switch (code) {
        case kByte:
        case kSignature:
        case kVariant:
            return WireU8::kWidth;
        case kInt16:
        case kUint16:
            return WireU16::kWidth;
        case kBoolean:
        case kInt32:
        case kUint32:
        case kString:
        case kObjectPath:
        case kArray:
            return WireU32::kWidth;
        case kInt64:
        case kUint64:
        case kDictOpen:
            return WireU64::kWidth;
        default:
            return WireU8::kWidth;
    }
}

[[nodiscard]] constexpr bool code_in_scope(char code) noexcept {
    switch (code) {
        case kByte:
        case kBoolean:
        case kInt16:
        case kUint16:
        case kInt32:
        case kUint32:
        case kInt64:
        case kUint64:
        case kString:
        case kObjectPath:
        case kSignature:
        case kVariant:
        case kArray:
        case kDictOpen:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool signature_valid(std::string_view signature, bool allow_sequence) noexcept;
[[nodiscard]] bool single_complete_type(std::string_view signature) noexcept;
[[nodiscard]] bool element_type_valid(std::string_view signature) noexcept;
[[nodiscard]] bool object_path_valid(std::string_view path) noexcept;
[[nodiscard]] bool names_valid(std::string_view name) noexcept;
[[nodiscard]] bool member_valid(std::string_view member) noexcept;

[[nodiscard]] inline WireU32 load_u32(
    std::span<const std::byte> bytes,
    BufferOffset at,
    bool big_endian
) noexcept {
    const std::span<const std::byte> word = at.at(bytes);
    return big_endian ? WireU32::of_be(word) : WireU32::of_le(word);
}

}
