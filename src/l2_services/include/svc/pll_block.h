// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "svc/pll_params.h"
#include "svc/pll_payload.h"

namespace mister::svc {

struct PllWrite {
    PllReg selector{};
    PllPayload payload{};

    friend constexpr bool operator==(const PllWrite&, const PllWrite&) = default;
};

struct PllBlock {
    static constexpr std::size_t kWrites = 6;

    static constexpr std::size_t kWordsPerWrite = 2;
    static constexpr std::size_t kItemWords = kWrites * kWordsPerWrite;

    std::array<PllWrite, kWrites> writes{};

    [[nodiscard]] static constexpr PllBlock from_item_words(
        std::span<const std::uint32_t, kItemWords> words
    ) noexcept {
        PllBlock b{};
        for (std::size_t w = 0; w < kWrites; ++w) {
            b.writes[w].selector = pll_reg_of(PllPayload{words[w * kWordsPerWrite]});
            b.writes[w].payload = PllPayload{words[w * kWordsPerWrite + 1]};
        }
        return b;
    }

    friend constexpr bool operator==(const PllBlock&, const PllBlock&) = default;
};

}
