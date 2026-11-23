// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "app/hps_framebuffer.h"
#include "app/video_pump_stats.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/coworker.h"
#include "os/child_process.h"
#include "os/clock.h"
#include "os/vt_console.h"
#include "ui/terminal_recipe.h"
#include "ui/terminal_script.h"

namespace mister::ui {

enum class TerminalPhase : std::uint8_t { Idle, Starting, Running, Ended, Failed, Detached };

class FbTerminalHost final : public xthread::ICoworker {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Wiring {
        app::HpsFramebuffer& fb;
        os::IVtSwitch& vt;
        const app::VideoStatsCell& video_stats;
        const os::IClock& clock;
        TerminalRecipe recipe;
    };

    explicit FbTerminalHost(Wiring w) noexcept;

    [[nodiscard]] Ex<void> launch(const TerminalScript& script) noexcept;
    void serve() noexcept override;
    void on_pause() noexcept override;
    void on_stop() noexcept override;

    void finish() noexcept;

    void detach() noexcept;

    [[nodiscard]] TerminalPhase phase() const noexcept { return phase_; }

    [[nodiscard]] bool child_running() const noexcept {
        return child_.has_value() && !child_->reaped();
    }

    [[nodiscard]] const TerminalRecipe& recipe() const noexcept { return w_.recipe; }

private:
    using Instant = os::RawMonotonicClock::time_point;

    void spawn_() noexcept;
    void reap_() noexcept;
    void drop_lease_() noexcept;
    void retry_drop_() noexcept;
    void reraise_() noexcept;
    [[nodiscard]] bool write_script_(const TerminalScript& script) noexcept;

    Wiring w_;
    std::optional<os::ChildProcess> child_{};
    std::optional<app::FbLease> lease_{};
    TerminalPhase phase_ = TerminalPhase::Idle;
    Instant settle_by_{};
    std::uint32_t applies_seen_ = 0;
    bool drop_owed_ = false;
};

}
