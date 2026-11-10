// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "app/types.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/loan_channel.h"
#include "infra/counter.h"
#include "infra/polled.h"

namespace mister::app {

class ScreenshotQueue {
    TASTY_SEAT_MEDIATOR(Ui, Diag);

public:
    static constexpr std::size_t kDepth = 4;
    static constexpr std::size_t kNameMax = 256;

    struct Shot {
        TASTY_SEAT_MEDIATOR(Any, Any);

        FixedStr<kNameMax, StrFit::Clip> path{};
        CorrelationTag tag{};
        bool scaled = false;
        bool ok = false;
    };

    using Chan = xthread::LoanChannel<Shot, kDepth, SeatTag::Ui, SeatTag::Diag>;
    using Pending = Chan::Loan;
    using Job = Chan::Job;

    ScreenshotQueue() noexcept
        : chan_(xthread::Polled{}, xthread::Polled{}),
          ui_(chan_.owner()),
          diag_(chan_.worker()) {}

    [[nodiscard]] Pending arm() noexcept {
        auto p = ui_.acquire();
        if (!p)
            drops_.add(1);
        return p;
    }

    void submit(Pending&& p) noexcept { ui_.send(std::move(p)); }

    [[nodiscard]] Job take() noexcept { return diag_.take(); }

    [[nodiscard]] Pending reap() noexcept { return ui_.reap(); }

    std::uint32_t drops() const noexcept { return drops_.get(); }

    std::uint32_t result_drops() const noexcept { return chan_.breaches(); }

private:
    Chan chan_;
    Chan::Owner ui_;
    Chan::Worker diag_;
    xthread::Counter drops_{};
};

}
