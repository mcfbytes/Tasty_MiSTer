// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>

#include "app/cmd_fifo.h"
#include "app/cmd_router.h"
#include "app/owner_tick.h"
#include "app/rt_main.h"
#include "app/screenshot_pump.h"
#include "app/ui_request.h"
#include "boot_parts.h"
#include "device_block.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "process_main.h"
#include "recorder_block.h"
#include "replay_block.h"
#include "tasty_cli.h"
#include "tasty_osd.h"
#include "tasty_sink.h"
#include "thread_assembly.h"

namespace mister::fw {

class TastyHub {
    TASTY_SEAT_EXEMPT(boot);

public:
    TastyHub(const BootParts& plat, app::IStopSignal& stop);

    ~TastyHub();
    TastyHub(const TastyHub&) = delete;
    TastyHub& operator=(const TastyHub&) = delete;

    app::SessionOwner& owner() noexcept { return device_.owner_; }
    void latch_boot_config() noexcept;
    ThreadAssembly& threads() noexcept { return assembly_; }
    app::RtMain& rt_main() noexcept { return rt_main_; }
    ProcessMain& process_main() noexcept { return process_main_; }
    const ThreadAssembly::SeatMains& seat_mains() const noexcept { return seat_mains_; }

    app::VideoPump& video_pump() noexcept { return device_.video_pump_; }
    app::ReplayFeeder& replay_feeder() noexcept { return replay_.replay_feeder_; }
    app::RecorderControl& recorder() noexcept { return rec_.recorder_; }
    const app::ReplayStatusCell& replay_status() const noexcept { return replay_.replay_status_; }
    const app::RecorderStatusCell& recorder_status() const noexcept { return rec_.rec_status_; }
    const app::EncodeStatusCell& encode_status() const noexcept { return rec_.encode_status_; }
    const svc::Vfs* vfs() const noexcept { return device_.vfs_; }
    void set_owner_tick(app::IOwnerTick* tick) noexcept {
        if (UiMain* const ui = assembly_.ui(); ui != nullptr) ui->set_owner_tick(tick);
    }
    void force_vsync_adjust(std::uint8_t v) noexcept { device_.video_pump_.force_vsync_adjust(v); }

    void arm_replay_ini(bool strict) noexcept {
        device_.video_pump_.arm_replay_ini(strict);
        device_.owner_.arm_replay_ini(strict);
    }
    const app::IdentityLatch& identity_latch() const noexcept {
        return device_.session_.identity();
    }
    app::UiRequestRing& ui_requests() noexcept { return device_.owner_.ui_requests(); }

    app::LinkTxChannel& ui_link_tx() noexcept { return device_.ui_inbox_; }
    [[nodiscard]] Ex<std::optional<app::UiRequest::LoadCore>> boot_link(
        std::span<const char* const> argv) {
        return device_.session_.boot(argv);
    }
    void seal_boot_pump() noexcept { device_.session_.seal_boot_pump(); }
    [[nodiscard]] Ex<void> shutdown_link() { return device_.session_.shutdown(); }
    void settle_writes() noexcept { device_.session_.settle_writes(); }
    [[nodiscard]] app::SessionState session_state() const noexcept {
        return device_.session_.state();
    }
    [[nodiscard]] Ex<void> pump_boot() { return rt_main_.pump_boot(); }

private:
    [[nodiscard]] DiagSampler::Sources diag_sources_(const BootParts& plat) noexcept;

    DeviceBlock device_;
    NullOsdClose null_osd_;
    TastySink tasty_sink_;
    app::ScreenshotPump shot_pump_;
    ReplayBlock replay_;
    RecorderBlock rec_;

    app::CmdRouter router_;
    std::optional<app::CmdFifo> fifo_;
    app::RtMain rt_main_;
    ThreadAssembly assembly_;
    ThreadAssembly::SeatMains seat_mains_{};
    ProcessMain process_main_;
};

[[nodiscard]] constexpr bool tasty_boot_calls_handoff() noexcept { return false; }
[[nodiscard]] int tasty_run_owner(const TastyArgs& args);

[[nodiscard]] const std::atomic<int>& tasty_owner_stop_flag() noexcept;
void tasty_arm_owner_signals() noexcept;

void tasty_owe_home() noexcept;

[[nodiscard]] int tasty_stop_and_owe() noexcept;

[[nodiscard]] bool tasty_home_may_launch() noexcept;

}  // namespace mister::fw
