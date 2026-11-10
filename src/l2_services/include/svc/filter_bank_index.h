// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "svc/types.h"

namespace mister::svc {

enum class FilterWireBank : std::uint8_t;

struct FilterBankIndex {
    std::size_t v = 0;

    [[nodiscard]] static constexpr FilterBankIndex of(FilterWireBank bank) noexcept {
        return FilterBankIndex{static_cast<std::size_t>(bank)};
    }

    [[nodiscard]] constexpr std::uint16_t wire_address(
        std::size_t per_bank_or_stride,
        std::size_t in_bank
    ) const noexcept {
        return static_cast<std::uint16_t>(v * per_bank_or_stride + in_bank);
    }

    friend constexpr bool operator==(FilterBankIndex, FilterBankIndex) = default;
};

constexpr FilterBankIndex FilterBankWordIndex::bank(FilterBankWordCount per_bank) const noexcept {
    return FilterBankIndex{v / per_bank.v};
}

}
