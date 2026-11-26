// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

#include "proto/types.h"

namespace mister::cores {

inline constexpr std::uint16_t kProgressSteps = 168;

struct Progress {
    std::uint16_t done = 0;
    std::uint16_t total = 0;

    [[nodiscard]] constexpr bool dismisses() const noexcept { return total == 0; }

    struct Bytes {
        proto::FileSize done{};
        proto::FileSize total{};
    };

    [[nodiscard]] static constexpr Progress of_bytes(Bytes b) noexcept {
        if (b.total.empty())
            return Progress{};
        return Progress{
            .done = static_cast<std::uint16_t>(std::min<std::uint64_t>(
                b.done.file_word<std::uint64_t>()
                    * kProgressSteps
                    / b.total.file_word<std::uint64_t>(),
                kProgressSteps
            )),
            .total = kProgressSteps
        };
    }

    [[nodiscard]] constexpr std::uint32_t scaled(std::uint32_t steps) const noexcept {
        if (dismisses())
            return 0;

        const std::uint32_t landed = done * steps / total;
        return std::min(landed, steps);
    }

    friend constexpr bool operator==(Progress, Progress) = default;
};

}
