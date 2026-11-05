// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "svc/filter_bank.h"
#include "svc/filter_store.h"
#include "svc/video_timing.h"

namespace mister::svc {

inline constexpr std::array<VFilter, kScalerSlots>
    kVFilters{VFilter::Horz, VFilter::Vert, VFilter::Scanlines, VFilter::Ilace};

static_assert(
    kVFilters[0] == VFilter::Horz
        && kVFilters[1] == VFilter::Vert
        && kVFilters[2] == VFilter::Scanlines
        && kVFilters[3] == VFilter::Ilace,
    "kVFilters must list every VFilter in enum order"
);

struct FilterSet {
    std::array<FilterBank, kScalerSlots> banks{};
    std::array<std::uint8_t, kScalerSlots> modes{};

    [[nodiscard]] const FilterBank& bank(VFilter f) const noexcept {
        for (std::size_t i = 0; i < kVFilters.size(); ++i)
            if (kVFilters[i] == f)
                return banks[i];
        return banks.front();
    }

    [[nodiscard]] std::array<bool, kScalerSlots> enabled() const noexcept {
        std::array<bool, kScalerSlots> out{};
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = modes[i] != 0;
        return out;
    }
};

}
