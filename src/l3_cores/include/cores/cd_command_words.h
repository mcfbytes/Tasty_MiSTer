// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "hal/types.h"

namespace mister::cores {

struct CdCommandWords {
    static constexpr std::size_t kMaxWords = 7;
    std::size_t count = 0;
    std::array<hal::SpiWord, kMaxWords> words{};

    [[nodiscard]] std::span<const hal::SpiWord> view() const noexcept {
        return {words.data(), count};
    }

    constexpr void store_le(std::span<std::uint8_t> out) const noexcept {
        for (std::size_t i = 0; i < count && (i + 1) * sizeof(hal::SpiWord) <= out.size(); ++i) {
            words[i].store_le(out.subspan(i * sizeof(hal::SpiWord)).first<sizeof(hal::SpiWord)>());
        }
    }

    friend constexpr bool operator==(const CdCommandWords&, const CdCommandWords&) = default;
};

}
