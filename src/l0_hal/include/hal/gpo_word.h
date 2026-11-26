// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/block_view.h"
#include "hal/gpo_block.h"
#include "hal/types.h"
#include "infra/prelude.h"

namespace mister::hal {

struct GpoWord {
    static constexpr std::uint32_t kData = 0x0000'FFFFu;

    std::uint32_t v = 0;

    [[nodiscard]] constexpr GpoWord with(GpoWord bits) const noexcept {
        return GpoWord{v | bits.v};
    }

    [[nodiscard]] constexpr GpoWord without(GpoWord bits) const noexcept {
        return GpoWord{v & ~bits.v};
    }

    [[nodiscard]] constexpr bool contains(GpoWord bits) const noexcept {
        return (v & bits.v) == bits.v;
    }

    [[nodiscard]] constexpr GpoWord with_data(LinkWord word) const noexcept {
        return GpoWord{(v & ~kData) | word.v};
    }

    [[nodiscard]] constexpr LinkWord data() const noexcept {
        return LinkWord{static_cast<std::uint16_t>(v & kData)};
    }

    [[nodiscard]] constexpr GpoWord masked(GpoWord mask) const noexcept {
        return GpoWord{v & mask.v};
    }

    void write_to(const BlockView<GpoBlock>& gpo) const noexcept {
        gpo.write(GpoBlock::Reg::Gpo, v);
    }

    [[nodiscard]] constexpr Error refusal(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(GpoWord, GpoWord) = default;
};

}
