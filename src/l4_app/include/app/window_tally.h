// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/load_window_counts.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

class WindowTally final {
    TASTY_SEAT_RESIDENT(Main);

public:
    using Cell = xthread::Telemetry<LoadWindowCounts, SeatTag::Main>;

    void refused(Errc why) noexcept {
        ++counts_.refusals;
        counts_.refusal_code = static_cast<std::uint16_t>(why);
    }

    void opened() noexcept { ++counts_.windows; }

    void cancelled() noexcept { ++counts_.cancelled; }

    void publish() noexcept { cell_.publish(counts_); }

    [[nodiscard]] const LoadWindowCounts& counts() const noexcept { return counts_; }

    [[nodiscard]] const Cell& cell() const noexcept { return cell_; }

private:
    LoadWindowCounts counts_{};
    Cell cell_{};
};

}
