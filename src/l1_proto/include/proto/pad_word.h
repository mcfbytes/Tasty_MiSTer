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

    template <class View>
        requires requires(const View& w, typename View::Reg r) { w.write(r, 0u); }
    void write_to(const View& view, typename View::Reg reg) const noexcept {
        view.write(reg, v);
    }

    friend constexpr bool operator==(PadWord, PadWord) = default;
};

}
