// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

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

[[nodiscard]] constexpr std::size_t save_size(MemoryType t) noexcept {
    switch (t) {
        case MemoryType::Eeprom512:
            return 0x200;
        case MemoryType::Eeprom2k:
            return 0x800;
        case MemoryType::Sram32k:
            return 0x8000;
        case MemoryType::Sram96k:
            return 0x18000;
        case MemoryType::Flash128k:
            return 0x20000;
        case MemoryType::Cpak:
        case MemoryType::Tpak:
            return 0x8000;
        case MemoryType::None:
            break;
    }
    return 0;
}

[[nodiscard]] constexpr std::string_view save_suffix(MemoryType t) noexcept;

struct SaveRowCount {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr SaveRowCount of(I rows) noexcept {
        return SaveRowCount{static_cast<std::uint8_t>(rows)};
    }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    [[nodiscard]] constexpr SaveRow end() const noexcept { return SaveRow{v}; }

    [[nodiscard]] constexpr SaveRow last() const noexcept {
        return SaveRow{static_cast<std::uint8_t>(v - 1u)};
    }

    [[nodiscard]] constexpr bool fits() const noexcept;
    friend constexpr bool operator==(SaveRowCount, SaveRowCount) = default;
};

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

[[nodiscard]] constexpr std::string_view save_type_text(MemoryType t) noexcept {
    switch (t) {
        case MemoryType::Eeprom512:
            return "4K EEPROM";
        case MemoryType::Eeprom2k:
            return "16K EEPROM";
        case MemoryType::Sram32k:
            return "256K SRAM";
        case MemoryType::Sram96k:
            return "768K SRAM";
        case MemoryType::Flash128k:
            return "Flash RAM";
        case MemoryType::Cpak:
            return "CPAK DATA";
        case MemoryType::Tpak:
            return "TPAK DATA";
        case MemoryType::None:
            break;
    }
    return "(none)";
}

[[nodiscard]] constexpr std::string_view save_suffix(MemoryType t) noexcept {
    switch (t) {
        case MemoryType::Eeprom512:
        case MemoryType::Eeprom2k:
            return ".eep";
        case MemoryType::Sram32k:
        case MemoryType::Sram96k:
            return ".sra";
        case MemoryType::Flash128k:
            return ".fla";
        case MemoryType::Cpak:
            return ".cpk";
        case MemoryType::Tpak:
            return ".tpk";
        case MemoryType::None:
            break;
    }
    return ".sav";
}

inline constexpr std::size_t kMaxSaveRows = 5;

constexpr bool SaveRowCount::fits() const noexcept {
    return v <= kMaxSaveRows;
}

[[nodiscard]] constexpr SaveRowCount rows_through(SaveRow row) noexcept {
    return SaveRowCount::of(row.v + 1u);
}

[[nodiscard]] constexpr bool needs_byteswap(MemoryType t) noexcept {
    return t == MemoryType::Cpak || t == MemoryType::Tpak;
}

[[nodiscard]] constexpr bool needs_byteswap(std::string_view suffix) noexcept {
    for (const MemoryType t :
         {MemoryType::Eeprom512,
          MemoryType::Eeprom2k,
          MemoryType::Sram32k,
          MemoryType::Sram96k,
          MemoryType::Flash128k,
          MemoryType::Cpak,
          MemoryType::Tpak}) {
        if (suffix == save_suffix(t))
            return needs_byteswap(t);
    }
    return false;
}

}
