// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "hal/types.h"
#include "infra/indexed_array.h"
#include "infra/span_cut.h"
#include "svc/types.h"

namespace mister::svc {

struct FilterWireImage {

    [[nodiscard]] std::span<const hal::LinkWord> words() const noexcept {
        return infra::affine::first(std::span<const hal::LinkWord>(slots), count);
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return count.empty(); }

    FilterWordCount count{};
    std::array<hal::LinkWord, kFilterWireWords> slots{};
};

inline void append_filter_word(FilterWireImage& image, hal::LinkWord word) noexcept {
    infra::at_index(image.slots, image.count.next_index()) = word;
    image.count = image.count.with_one_more();
}

}
