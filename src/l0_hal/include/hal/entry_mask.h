// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "hal/reg_word.h"
#include "hal/sys_directory.h"
#include "infra/json_out.h"

namespace mister::hal {

struct EntryMask {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr EntryMask of(RegWord w) noexcept { return EntryMask{w.bits()}; }

    [[nodiscard]] constexpr RegWord register_word() const noexcept { return RegWord{v}; }

    [[nodiscard]] constexpr EntryMask with(EntryMask bits) const noexcept {
        return EntryMask{v | bits.v};
    }

    [[nodiscard]] constexpr EntryMask minus(EntryMask bits) const noexcept {
        return EntryMask{v & ~bits.v};
    }

    [[nodiscard]] constexpr bool has(EntryMask bits) const noexcept { return (v & bits.v) != 0u; }

    [[nodiscard]] constexpr bool covers(EntryMask want) const noexcept {
        return (v & want.v) == want.v;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    friend constexpr void json_field(infra::JsonOut& o, infra::JsonName key, EntryMask m) noexcept {
        o.field(key, m.v);
    }

    friend constexpr bool operator==(EntryMask, EntryMask) = default;
};

[[nodiscard]] constexpr EntryMask entry_bit(SysDirectory::EntryIndex i) noexcept {
    return EntryMask{1u << std::to_underlying(i)};
}

}
