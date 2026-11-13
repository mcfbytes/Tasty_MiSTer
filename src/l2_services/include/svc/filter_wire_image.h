// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "infra/indexed_array.h"
#include "svc/types.h"

namespace mister::svc {

struct FilterWireImage {

    [[nodiscard]] std::span<const std::uint16_t> words() const noexcept {
        return count.prefix_of(std::span<const std::uint16_t>(slots));
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return count.empty(); }

    FilterWordCount count{};
    std::array<std::uint16_t, kFilterWireWords> slots{};
};

inline void append_filter_word(FilterWireImage& image, std::uint16_t word) noexcept {
    infra::at_index(image.slots, image.count.next_index()) = word;
    image.count = image.count.with_one_more();
}

}
