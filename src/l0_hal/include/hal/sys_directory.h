// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "hal/reg_access.h"
#include "hal/reg_word.h"
#include "hal/types.h"

namespace mister::hal {

struct SysDirectory {
    static constexpr RegWord kMagic{0x5244574Cu};
    static constexpr std::uint32_t kMajor = 2;
    static constexpr std::uint32_t kMaxEntries = 31;
    static constexpr std::uint32_t kHeaderWords = 16;
    static constexpr std::size_t kSize = 0x1000;

    static constexpr RegWord kKindMask{0xFFFFu};

    static constexpr LwOffset kPageEnd{static_cast<std::uint32_t>(kSize)};

    static constexpr RegAccess kAccess = RegAccess::ReadWrite;

    enum class Reg : std::uint32_t {
        Magic = 0x00,
        Version = 0x04,
        Layout = 0x08,
        Count = 0x0C,
        WinBase = 0x10,
        WinSize = 0x14,
        Enable = 0x18,
        Pending = 0x20,
        Owned = 0x24,
        Scratch = 0x28,
        Open = 0x2C,
        LineEn = 0x30,
        Resets = 0x34,
    };

    enum class EntryIndex : std::uint32_t { kFirst = 0, kLast = kMaxEntries - 1 };

    [[nodiscard]] static constexpr std::size_t dword_index(Reg r) noexcept {
        return LwOffset{std::to_underlying(r)}.dword_index();
    }

    [[nodiscard]] static constexpr Reg reg_at(LwOffset at) noexcept {
        return static_cast<Reg>(at.v);
    }
};

[[nodiscard]] constexpr SysDirectory::EntryIndex next_entry(SysDirectory::EntryIndex i) noexcept {
    return static_cast<SysDirectory::EntryIndex>(static_cast<std::uint32_t>(i) + 1u);
}

}
