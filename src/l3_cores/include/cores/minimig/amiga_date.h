// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ratio>

namespace mister::cores::minimig {

struct AmigaDate {

    using Days = std::chrono::duration<std::int32_t, std::chrono::days::period>;
    using Minutes = std::chrono::duration<std::int32_t, std::chrono::minutes::period>;
    using Ticks = std::chrono::duration<std::int32_t, std::ratio<1, 50>>;

    Days days{};
    Minutes minutes{};
    Ticks ticks{};

    [[nodiscard]] static constexpr AmigaDate of(std::chrono::sys_seconds at) noexcept {
        using std::chrono::duration_cast;
        const std::chrono::seconds t = at.time_since_epoch();
        const auto whole = duration_cast<std::chrono::days>(t);
        const auto mins = duration_cast<std::chrono::minutes>(t - whole);
        const auto since = std::max(whole - kEpoch, std::chrono::days::zero());
        return AmigaDate{
            .days = duration_cast<Days>(since),
            .minutes = duration_cast<Minutes>(mins),
            .ticks = duration_cast<Ticks>(t - whole - mins)
        };
    }

    friend constexpr bool operator==(AmigaDate, AmigaDate) = default;

private:
    static constexpr std::chrono::days kEpoch{2922};
};

static_assert(
    AmigaDate::of(
        std::chrono::sys_days{std::chrono::year{1978} / 1 / 2}
        + std::chrono::minutes{61}
        + std::chrono::seconds{3}
    )
    == AmigaDate{
        .days = AmigaDate::Days{1},
        .minutes = AmigaDate::Minutes{61},
        .ticks = AmigaDate::Ticks{150}
    }
);

}
