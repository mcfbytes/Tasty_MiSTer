// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::hal {

struct SysDirectory {
    enum class Reg : std::uint32_t { Magic = 0x0, Version = 0x4, Count = 0x8, Scratch = 0xC };
    static constexpr std::uint32_t kMagic = 0x31535953u;
    static constexpr std::uint32_t kMajor = 1;
    static constexpr std::uint32_t kEntryBase = 0x10;
    static constexpr std::uint32_t kEntryStride = 0x40;
    static constexpr std::uint32_t kMaxEntries = 8;
    static constexpr std::size_t kSize = kEntryBase + kEntryStride * kMaxEntries;

    static constexpr Reg entry(std::uint32_t i, std::uint32_t word) noexcept {
        return static_cast<Reg>(kEntryBase + kEntryStride * i + 4u * word);
    }
};

static_assert(
    static_cast<std::uint32_t>(SysDirectory::entry(SysDirectory::kMaxEntries - 1, 15))
    == SysDirectory::kSize - 4
);

}
