// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/stick_pos.h"
#include "proto/types.h"

namespace mister::proto {

struct PadWord {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr PadWord of(JoyMask map) noexcept { return PadWord{map.v}; }

    [[nodiscard]] static constexpr PadWord of(StickPos pos) noexcept {
        return PadWord{pos.packed()};
    }

    [[nodiscard]] constexpr JoyMask buttons() const noexcept { return JoyMask{v}; }

    [[nodiscard]] constexpr StickPos stick() const noexcept { return StickPos::from_packed(v); }

    template <class Port>
        requires requires(Port& p, typename Port::Reg r) { p.write(r, hal::RegWord{}); }
    void write_to(Port& port, typename Port::Reg reg) const noexcept {
        port.write(reg, hal::RegWord{v});
    }

    friend constexpr bool operator==(PadWord, PadWord) = default;
};

}
