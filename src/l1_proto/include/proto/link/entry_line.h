// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/reg_word.h"
#include "proto/types.h"

namespace mister::proto::link {

struct EntryLine {
    enum : std::uint32_t {
        kFrameworkFirst = 1,
        kFrameworkLast = 15,
        kPrivateFirst = 16,
        kPrivateLast = 31
    };

    static constexpr hal::RegWord kOrdinalField{0xFFu};

    IrqOrdinal ordinal{};

    [[nodiscard]] static constexpr EntryLine of(hal::RegWord w) noexcept {
        return EntryLine{IrqOrdinal::of(w.masked(kOrdinalField))};
    }

    [[nodiscard]] constexpr bool declared() const noexcept { return ordinal != IrqOrdinal{}; }

    [[nodiscard]] constexpr bool framework() const noexcept {
        return ordinal >= IrqOrdinal{kFrameworkFirst} && ordinal <= IrqOrdinal{kFrameworkLast};
    }

    [[nodiscard]] constexpr bool names_same(EntryLine other) const noexcept {
        return declared() && ordinal <= IrqOrdinal{kPrivateLast} && ordinal == other.ordinal;
    }

    friend constexpr bool operator==(EntryLine, EntryLine) = default;
};

}
