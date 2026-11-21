// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/block_view.h"
#include "hal/gpo_block.h"
#include "hal/types.h"
#include "infra/prelude.h"

namespace mister::hal {

struct SpiOutput {
    static constexpr std::uint32_t kData = 0x0000'FFFFu;

    std::uint32_t v = 0;

    [[nodiscard]] constexpr SpiOutput with(SpiOutput bits) const noexcept {
        return SpiOutput{v | bits.v};
    }

    [[nodiscard]] constexpr SpiOutput without(SpiOutput bits) const noexcept {
        return SpiOutput{v & ~bits.v};
    }

    [[nodiscard]] constexpr bool contains(SpiOutput bits) const noexcept {
        return (v & bits.v) == bits.v;
    }

    [[nodiscard]] constexpr SpiOutput with_data(SpiWord word) const noexcept {
        return SpiOutput{(v & ~kData) | word.v};
    }

    [[nodiscard]] constexpr SpiWord data() const noexcept {
        return SpiWord{static_cast<std::uint16_t>(v & kData)};
    }

    [[nodiscard]] constexpr SpiOutput masked(SpiOutput mask) const noexcept {
        return SpiOutput{v & mask.v};
    }

    void write_to(const BlockView<GpoBlock>& gpo) const noexcept {
        gpo.write(GpoBlock::Reg::Gpo, v);
    }

    [[nodiscard]] constexpr Error refusal(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(SpiOutput, SpiOutput) = default;
};

}
