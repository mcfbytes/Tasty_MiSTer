// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cores/save_row_count.h"
#include "cores/save_partition.h"
#include "cores/types.h"
#include "proto/types.h"

namespace mister::cores::n64 {

enum class MemoryType : std::uint32_t {
    None = 0,
    Eeprom512,
    Eeprom2k,
    Sram32k,
    Sram96k,
    Flash128k,
    Cpak = ~2U,
    Tpak = ~1U,
};

namespace detail {

struct MemoryTypeRow {
    MemoryType type;
    std::size_t size;
    std::string_view text;
    std::string_view suffix;
    bool byteswap;
};

inline constexpr std::size_t kMemoryTypePakBytes = 0x8000;

inline constexpr auto kMemoryTypes = std::to_array<MemoryTypeRow>({
    {.type = MemoryType::None, .size = 0, .text = "(none)", .suffix = ".sav", .byteswap = false},
    {.type = MemoryType::Eeprom512,
     .size = 0x200,
     .text = "4K EEPROM",
     .suffix = ".eep",
     .byteswap = false},
    {.type = MemoryType::Eeprom2k,
     .size = 0x800,
     .text = "16K EEPROM",
     .suffix = ".eep",
     .byteswap = false},
    {.type = MemoryType::Sram32k,
     .size = 0x8000,
     .text = "256K SRAM",
     .suffix = ".sra",
     .byteswap = false},
    {.type = MemoryType::Sram96k,
     .size = 0x18000,
     .text = "768K SRAM",
     .suffix = ".sra",
     .byteswap = false},
    {.type = MemoryType::Flash128k,
     .size = 0x20000,
     .text = "Flash RAM",
     .suffix = ".fla",
     .byteswap = false},
    {.type = MemoryType::Cpak,
     .size = kMemoryTypePakBytes,
     .text = "CPAK DATA",
     .suffix = ".cpk",
     .byteswap = true},
    {.type = MemoryType::Tpak,
     .size = kMemoryTypePakBytes,
     .text = "TPAK DATA",
     .suffix = ".tpk",
     .byteswap = true},
});

consteval bool one_row_per_memory_type() {
    std::size_t enumerated = 0;
    switch (MemoryType::None) {
        case MemoryType::None:
            ++enumerated;
            [[fallthrough]];
        case MemoryType::Eeprom512:
            ++enumerated;
            [[fallthrough]];
        case MemoryType::Eeprom2k:
            ++enumerated;
            [[fallthrough]];
        case MemoryType::Sram32k:
            ++enumerated;
            [[fallthrough]];
        case MemoryType::Sram96k:
            ++enumerated;
            [[fallthrough]];
        case MemoryType::Flash128k:
            ++enumerated;
            [[fallthrough]];
        case MemoryType::Cpak:
            ++enumerated;
            [[fallthrough]];
        case MemoryType::Tpak:
            ++enumerated;
            break;
    }
    if (enumerated != kMemoryTypes.size())
        return false;
    for (std::size_t i = 0; i < kMemoryTypes.size(); ++i)
        for (std::size_t j = i + 1; j < kMemoryTypes.size(); ++j)
            if (kMemoryTypes[i].type == kMemoryTypes[j].type)
                return false;
    return true;
}

static_assert(
    one_row_per_memory_type(),
    "kMemoryTypes: exactly one row per MemoryType, each naming its own type"
);

[[nodiscard]] constexpr const MemoryTypeRow& memory_type_row(MemoryType t) noexcept {
    for (const MemoryTypeRow& row : kMemoryTypes)
        if (row.type == t)
            return row;
    return kMemoryTypes.front();
}

}

[[nodiscard]] constexpr std::size_t save_size(MemoryType t) noexcept {
    return detail::memory_type_row(t).size;
}

[[nodiscard]] constexpr std::string_view save_type_text(MemoryType t) noexcept {
    return detail::memory_type_row(t).text;
}

[[nodiscard]] constexpr std::string_view save_suffix(MemoryType t) noexcept {
    return detail::memory_type_row(t).suffix;
}

[[nodiscard]] constexpr MemoryType save_type_of(proto::FieldValue code) noexcept {
    const auto t = static_cast<MemoryType>(code.v);
    return save_size(t) != 0 ? t : MemoryType::None;
}

[[nodiscard]] constexpr proto::FieldValue field_of(MemoryType t) noexcept {
    return proto::FieldValue{static_cast<std::uint32_t>(t)};
}

[[nodiscard]] constexpr SavePartition save_partition(std::string_view name, MemoryType t) noexcept {
    return SavePartition{
        .name = name,
        .suffix = save_suffix(t),
        .length = static_cast<std::uint32_t>(save_size(t))
    };
}

[[nodiscard]] constexpr std::uint32_t save_offset(
    SaveRowCount before,
    MemoryType cart,
    bool tpak
) noexcept {
    std::size_t idx = before.size();
    std::uint32_t off = 0;
    if (idx != 0 && cart != MemoryType::None) {
        off += save_partition({}, cart).length;
        --idx;
    }
    if (idx != 0 && tpak) {
        off += save_partition({}, MemoryType::Tpak).length;
        --idx;
    }
    off += static_cast<std::uint32_t>(save_partition({}, MemoryType::Cpak).length * idx);
    return off;
}

enum class SaveRowKind : std::uint8_t { Cart, Tpak, Pak1, Pak2, Pak3, Pak4 };

[[nodiscard]] constexpr std::string_view save_row_name(SaveRowKind k) noexcept {
    switch (k) {
        case SaveRowKind::Cart:
            return "cart";
        case SaveRowKind::Tpak:
            return "tpak";
        case SaveRowKind::Pak1:
            return "pak1";
        case SaveRowKind::Pak2:
            return "pak2";
        case SaveRowKind::Pak3:
            return "pak3";
        case SaveRowKind::Pak4:
            return "pak4";
    }
    return {};
}

[[nodiscard]] constexpr MemoryType save_row_type(SaveRowKind k, MemoryType cart) noexcept {
    switch (k) {
        case SaveRowKind::Cart:
            return cart;
        case SaveRowKind::Tpak:
            return MemoryType::Tpak;
        case SaveRowKind::Pak1:
        case SaveRowKind::Pak2:
        case SaveRowKind::Pak3:
        case SaveRowKind::Pak4:
            return MemoryType::Cpak;
    }
    return MemoryType::None;
}

[[nodiscard]] constexpr SavePartition save_row_partition(SaveRowKind k, MemoryType cart) noexcept {
    SavePartition row = save_partition(save_row_name(k), save_row_type(k, cart));
    switch (k) {
        case SaveRowKind::Cart:
            break;
        case SaveRowKind::Tpak:
        case SaveRowKind::Pak1:
            row.digit = SaveDigit::of(1);
            break;
        case SaveRowKind::Pak2:
            row.digit = SaveDigit::of(2);
            break;
        case SaveRowKind::Pak3:
            row.digit = SaveDigit::of(3);
            break;
        case SaveRowKind::Pak4:
            row.digit = SaveDigit::of(4);
            break;
    }
    return row;
}

[[nodiscard]] constexpr bool needs_byteswap(MemoryType t) noexcept {
    return detail::memory_type_row(t).byteswap;
}

[[nodiscard]] constexpr bool needs_byteswap(std::string_view suffix) noexcept {
    for (const detail::MemoryTypeRow& row : detail::kMemoryTypes)
        if (suffix == row.suffix)
            return needs_byteswap(row.type);
    return false;
}

}
