// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "cores/cmos_layout.h"
#include "cores/ladder_host.h"
#include "cores/payload_pieces.h"
#include "hal/types.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "os/clock.h"
#include "os/deadline.h"
#include "proto/types.h"

namespace mister::app {

class CmosKeeper {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr std::chrono::seconds kSavePeriod{1};
    static constexpr std::chrono::seconds kClockPeriod{60};

    static constexpr proto::FioWordCount kClockPieceWords{16};

    explicit CmosKeeper(const os::IClock& clock) noexcept : clock_(clock) {}

    void bind(infra::OptRef<const cores::CmosLayout> layout) noexcept;

    [[nodiscard]] bool bound() const noexcept { return static_cast<bool>(layout_); }

    void on_change_count(hal::LinkWord word) noexcept;

    void tick(cores::ILadderHost& host);

    [[nodiscard]] std::optional<os::Deadline> next_due() const noexcept;

    [[nodiscard]] infra::Tally pulls_ordered() const noexcept { return pulls_ordered_; }

    [[nodiscard]] infra::Tally clocks_sent() const noexcept { return clocks_sent_; }

private:
    void step_clock_(cores::ILadderHost& host);

    const os::IClock& clock_;
    infra::OptRef<const cores::CmosLayout> layout_{};
    hal::LinkWord count_{};
    hal::LinkWord pulled_{};
    os::Deadline save_due_{os::Deadline::immediate()};
    os::Deadline clock_due_{os::Deadline::immediate()};
    std::optional<cores::PayloadPieces> clock_run_{};
    infra::Tally pulls_ordered_{};
    infra::Tally clocks_sent_{};
};

}
