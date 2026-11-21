// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "cores/types.h"

namespace mister::cores::mra {

struct FillRun {
    RomOffset at{};
    RomLength len{};
    std::vector<std::uint8_t> pattern;

    [[nodiscard]] RomOffset end() const noexcept { return at + len; }

    void write_to(std::span<std::uint8_t> out, RomLength phase) const noexcept {
        if (pattern.size() == 1) {
            std::ranges::fill(out, pattern.front());
            return;
        }
        const std::span<const std::uint8_t> whole{pattern};
        std::span<const std::uint8_t> next = phase.phase_in(whole);
        for (std::uint8_t& b : out) {
            if (next.empty())
                next = whole;
            b = next.front();
            next = next.subspan(1);
        }
    }
};

}
