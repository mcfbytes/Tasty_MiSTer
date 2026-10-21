// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

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

[[nodiscard]] constexpr std::uint32_t save_offset(
    std::size_t idx,
    MemoryType cart,
    bool tpak
) noexcept {
    std::uint32_t off = 0;
    if (idx != 0 && cart != MemoryType::None) {
        off += static_cast<std::uint32_t>(save_size(cart));
        --idx;
    }
    if (idx != 0 && tpak) {
        off += static_cast<std::uint32_t>(save_size(MemoryType::Tpak));
        --idx;
    }
    off += static_cast<std::uint32_t>(save_size(MemoryType::Cpak) * idx);
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

inline constexpr std::uint8_t kLayoutCartMask = 0x07;
inline constexpr std::uint8_t kLayoutTpakBit = 0x08;
inline constexpr unsigned kLayoutCountShift = 4;

[[nodiscard]] constexpr std::uint8_t save_layout_key(
    MemoryType cart,
    bool tpak,
    std::size_t rows
) noexcept {
    const auto raw = static_cast<std::uint32_t>(cart);

    const auto c = static_cast<std::uint8_t>(
        raw <= static_cast<std::uint32_t>(MemoryType::Flash128k) ? raw : 0u
    );
    const auto n = static_cast<std::uint8_t>(rows > kMaxSaveRows ? 0u : rows);
    return static_cast<std::uint8_t>(
        c | (tpak ? kLayoutTpakBit : 0u) | static_cast<std::uint8_t>(n << kLayoutCountShift)
    );
}

struct SaveLayout {
    std::uint32_t offset[kMaxSaveRows]{};
    std::uint32_t length[kMaxSaveRows]{};
    std::uint8_t count = 0;

    bool valid = false;
};

[[nodiscard]] constexpr SaveLayout expand_save_layout(std::uint8_t key) noexcept {
    SaveLayout out{};
    const auto cart = static_cast<MemoryType>(key & kLayoutCartMask);
    const bool tpak = (key & kLayoutTpakBit) != 0;
    const auto n = static_cast<std::size_t>(key >> kLayoutCountShift);
    if (n == 0 || n > kMaxSaveRows)
        return out;
    std::uint32_t prev = save_offset(0, cart, tpak);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t next = save_offset(i + 1, cart, tpak);
        if (next <= prev)
            return SaveLayout{};
        out.offset[i] = prev;
        out.length[i] = next - prev;
        prev = next;
    }
    out.count = static_cast<std::uint8_t>(n);
    out.valid = true;
    return out;
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
