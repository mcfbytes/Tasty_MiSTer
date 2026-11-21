// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/dir_count.h"
#include "hal/reg_word.h"
#include "hal/sys_directory.h"
#include "hal/types.h"

namespace mister::hal {

struct DirLayout {

    static constexpr LwExtent kMinHeader{0x40};
    static constexpr LwExtent kMinStride{0x20};
    static constexpr unsigned kLenBits = 16;

    std::uint32_t v = 0;

    [[nodiscard]] static constexpr DirLayout of(RegWord w) noexcept { return DirLayout{w.bits()}; }

    [[nodiscard]] constexpr RegWord register_word() const noexcept { return RegWord{v}; }

    [[nodiscard]] constexpr LwExtent header() const noexcept {
        return LwExtent{v & ((1u << kLenBits) - 1u)};
    }

    [[nodiscard]] constexpr LwExtent entry_stride() const noexcept {
        return LwExtent{v >> kLenBits};
    }

    [[nodiscard]] constexpr bool admits() const noexcept {
        return header().covers(kMinHeader)
            && entry_stride().covers(kMinStride)
            && header().from_origin().dword_aligned()
            && entry_stride().from_origin().dword_aligned();
    }

    [[nodiscard]] constexpr std::optional<LwOffset> table_end(DirCount n) const noexcept;

    [[nodiscard]] constexpr LwOffset entry_at(SysDirectory::EntryIndex i) const noexcept;

    [[nodiscard]] constexpr bool fits(DirCount n) const noexcept;
};

constexpr std::optional<LwOffset> DirLayout::table_end(DirCount n) const noexcept {
    const std::optional<LwExtent> entries = n.span(entry_stride());
    if (!entries)
        return std::nullopt;
    return header().from_origin().plus(entries->from_origin());
}

constexpr LwOffset DirLayout::entry_at(SysDirectory::EntryIndex i) const noexcept {
    return table_end(DirCount::preceding(i)).value_or(SysDirectory::kPageEnd);
}

constexpr bool DirLayout::fits(DirCount n) const noexcept {
    const std::optional<LwOffset> end = table_end(n);
    return end && *end <= SysDirectory::kPageEnd;
}

}
