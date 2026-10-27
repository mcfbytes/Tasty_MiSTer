// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>

#include "app/cmd_fifo.h"
#include "app/cmd_router.h"
#include "app/link_session.h"
#include "app/rt_main.h"
#include "app/screenshot_pump.h"
#include "app/session_owner.h"
#include "app/video_pump.h"
#include "boot_parts.h"
#include "device_block.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "process_main.h"
#include "recorder_block.h"
#include "replay_block.h"
#include "session_mode.h"
#include "thread_assembly.h"

namespace mister::fw {

class Hub {
    TASTY_SEAT_EXEMPT(boot);

public:
    [[nodiscard]] static Ex<std::unique_ptr<Hub>> create(
        const BootParts& plat,
        app::IStopSignal& stop,
        SessionMode& mode
    );

    ~Hub();
    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;

    app::LinkSession& link_session() noexcept { return device_.session_; }

    app::SessionOwner& owner() noexcept { return device_.owner_; }

    xthread::WakeFlag& main_stop() noexcept { return device_.main_stop_; }

    void latch_boot_config() noexcept;

    ThreadAssembly& threads() noexcept { return assembly_; }

    app::RtMain& rt_main() noexcept { return rt_main_; }

    ProcessMain& process_main() noexcept {
        return seated(process_main_, "MAIN's main read past a refused open");
    }

    const ThreadAssembly::SeatMains& seat_mains() const noexcept { return seat_mains_; }

private:
    Hub(const BootParts& plat, app::IStopSignal& stop, SessionMode& mode);

    [[nodiscard]] DiagSampler::Sources diag_sources_(const BootParts& plat) noexcept;

    std::unique_ptr<SessionMode::Front> front_;
    DeviceBlock device_;

    std::unique_ptr<SessionMode::Face> face_;
    const SessionMode::Face::Parts face_parts_;
    app::ScreenshotPump shot_pump_;
    ReplayBlock replay_;
    RecorderBlock rec_;
    std::unique_ptr<SessionMode::Tail> tail_;

    app::CmdRouter router_;
    std::optional<app::CmdFifo> fifo_;
    app::RtMain rt_main_;

    ThreadAssembly assembly_;
    ThreadAssembly::SeatMains seat_mains_{};
    std::optional<ProcessMain> process_main_;
};

}
