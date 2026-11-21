// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <optional>

#include "app/hd/hd_osd_ask.h"
#include "app/idle_knobs.h"
#include "app/input/raw_key_edge.h"
#include "infra/seat.h"
#include "os/deadline.h"
#include "infra/tally.h"
#include "svc/types.h"

namespace mister::ui {

class FrontEndIdleClock {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::chrono::seconds kOsdTimeoutMin{5};

    static constexpr std::chrono::seconds kDarkLogoRearm{10};

    struct Inputs {
        os::RawMonotonicClock::time_point now{};

        bool any_key = false;

        bool on_front_page = false;
        bool front_end = false;

        bool knobs_published = false;
        app::IdleKnobs knobs{};
    };

    struct Result {
        app::hd::FrontEndIdle idle = app::hd::FrontEndIdle::Awake;
        std::optional<bool> ctl{};
    };

    Result round(const Inputs& in) noexcept;

    void ctl_pushed(bool landed) noexcept;

    bool on_key(const app::input::RawKeyEdge& edge) noexcept;

    [[nodiscard]] app::hd::FrontEndIdle idle() const noexcept { return idle_; }

    [[nodiscard]] infra::Tally menu_ctls() const noexcept { return ctls_; }

    [[nodiscard]] infra::Tally menu_ctl_refusals() const noexcept { return refusals_; }

    [[nodiscard]] infra::Tally dark_logo_rearms() const noexcept { return rearm_; }

private:
    void wake_(Result& out) noexcept;

    app::hd::FrontEndIdle idle_ = app::hd::FrontEndIdle::Awake;

    std::optional<os::Deadline> deadline_{};
    std::optional<os::Deadline> off_deadline_{};

    svc::EvdevCode release_code_{};
    bool release_owed_ = false;

    bool wake_seen_ = false;

    std::optional<bool> owed_{};
    std::optional<bool> offered_{};
    infra::Tally ctls_{};
    infra::Tally refusals_{};
    infra::Tally rearm_{};
};

}
