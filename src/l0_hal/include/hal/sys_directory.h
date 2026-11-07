// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::hal {

struct SysDirectory {
    enum class Reg : std::uint32_t { Magic = 0x0, Version = 0x4, Count = 0x8, Scratch = 0xC };

    enum class EntryIndex : std::uint32_t {};
    enum class WordInEntry : std::uint32_t {};
    static constexpr std::uint32_t kMagic = 0x31535953u;
    static constexpr std::uint32_t kMajor = 1;
    static constexpr std::uint32_t kEntryBase = 0x10;
    static constexpr std::uint32_t kEntryStride = 0x40;
    static constexpr std::uint32_t kMaxEntries = 8;
    static constexpr std::size_t kSize = kEntryBase + kEntryStride * kMaxEntries;

    [[nodiscard]] static constexpr std::uint32_t offset_of(
        EntryIndex i,
        WordInEntry word
    ) noexcept {
        return kEntryBase
            + kEntryStride * static_cast<std::uint32_t>(i)
            + static_cast<std::uint32_t>(sizeof(std::uint32_t)) * static_cast<std::uint32_t>(word);
    }

    static constexpr Reg entry(EntryIndex i, WordInEntry word) noexcept {
        return static_cast<Reg>(offset_of(i, word));
    }
};

static_assert(
    SysDirectory::entry(
        SysDirectory::EntryIndex{SysDirectory::kMaxEntries - 1},
        SysDirectory::WordInEntry{15}
    )
    == static_cast<SysDirectory::Reg>(SysDirectory::kSize - 4)
);

}
