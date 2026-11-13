// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "app/owner_tick.h"
#include "app/encode_status.h"
#include "app/output_size.h"
#include "app/path_text.h"
#include "app/play_refusal.h"
#include "app/recorder_status.h"
#include "app/replay_feeder.h"
#include "app/replay_status.h"
#include "app/step_count.h"
#include "cores/movie_codec.h"
#include "cores/movie_system.h"
#include "hal/phys_region.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "infra/wake_flag.h"
#include "os/monotonic_clock.h"
#include "replay_port.h"
#include "run_exit.h"
#include "tasty_cli.h"
#include "tasty_refusal.h"
#include "tasty_splash.h"

namespace mister::app {
class IdentityLatch;
template <SeatTag Seat>
class LinkTxChannel;
class RecorderControl;
class UiRequestRing;
class VideoPump;
}

namespace mister::svc {
class Vfs;
}

namespace mister::fw {

class TastyStatusFile;

using StatusText = FixedStr<768, StrFit::Reject>;

class TastySession final : public app::IOwnerTick {
    TASTY_SEAT_RESIDENT(Ui);

public:
    class Run {
        TASTY_SEAT_EXEMPT(boot);

    public:
        virtual ~Run() = default;

        [[nodiscard]] virtual bool loads_core() const noexcept = 0;

        [[nodiscard]] virtual bool replays() const noexcept = 0;

        [[nodiscard]] virtual bool ends_with_recording() const noexcept = 0;

        [[nodiscard]] virtual bool arms_recording() const noexcept = 0;
    };

    class PlayRun final : public Run {
        TASTY_SEAT_EXEMPT(boot);

    public:
        [[nodiscard]] bool loads_core() const noexcept override { return true; }

        [[nodiscard]] bool replays() const noexcept override { return true; }

        [[nodiscard]] bool ends_with_recording() const noexcept override { return false; }

        [[nodiscard]] bool arms_recording() const noexcept override { return true; }
    };

    class RecordOnlyRun final : public Run {
        TASTY_SEAT_EXEMPT(boot);

    public:
        [[nodiscard]] bool loads_core() const noexcept override { return false; }

        [[nodiscard]] bool replays() const noexcept override { return false; }

        [[nodiscard]] bool ends_with_recording() const noexcept override { return true; }

        [[nodiscard]] bool arms_recording() const noexcept override { return false; }
    };

    struct Wiring {
        infra::OptRef<app::VideoPump> video{};
        IReplayPort& replay;
        app::RecorderControl& rec;
        app::UiRequestRing& asks;
        app::LinkTxChannel<SeatTag::Ui>& link_tx;
        const app::IdentityLatch& identity;
        const app::ReplayStatusCell& play;
        const app::RecorderStatusCell& recstat;
        const app::EncodeStatusCell& encstat;
        xthread::WakeFlag& main_stop;
        const svc::Vfs& vfs;
        const std::atomic<int>& stop;

        RunExitCell& run_exit;

        TastyStatusFile& status;
        hal::PhysRegion video_fb{};

        const xthread::Telemetry<std::uint8_t, SeatTag::Main>& direct_video_ini;
    };

    TastySession(const Wiring& w, const TastyArgs& args, const Run& run) noexcept;
    void tick() noexcept override;

private:
    enum class Stage : std::uint8_t {
        HideOsd,
        SplashMap,
        SplashBlit,
        SplashShow,
        SplashWait,
        SplashHide,
        LoadCore,
        WaitId,
        Start,
        Run,
        ReturnWait,
        LoadMenu,
        WaitMenu,
        Done,
    };

    void write_status_(bool force = false) noexcept;

    [[nodiscard]] bool replay_refused_() const noexcept { return w_.replay.refusal().has_value(); }

    [[nodiscard]] bool scaler_port_stuck_() const noexcept;
    [[nodiscard]] bool rec_only_over_() noexcept;

    void sample_rec_failure_() noexcept;

    void note_rec_failure_(const app::RecorderStatus& rec) noexcept;
    [[nodiscard]] bool ask_core_(const app::PathText& path) noexcept;
    [[nodiscard]] bool core_ready_() const noexcept;

    [[nodiscard]] bool video_reloaded_() const noexcept;
    [[nodiscard]] bool menu_ready_() const noexcept;

    [[nodiscard]] bool begin_play_() noexcept;

    [[nodiscard]] bool run_head_() noexcept;

    [[nodiscard]] app::OutputSize splash_size_() const noexcept;

    void on_replay_end_(
        app::ReplayEnd end,
        app::EpochFail epoch_fail,
        std::chrono::nanoseconds now
    ) noexcept;
    void fail_(const char* why) noexcept;

    [[nodiscard]] bool copy_seed_() noexcept;
    static constexpr proto::FileSize kSeedMax{1u << 20};
    void fail_refusal_() noexcept;
    void go_menu_() noexcept;
    void print_settings_() noexcept;
    [[nodiscard]] Stage after_hide_() const noexcept;

    [[nodiscard]] Stage after_splash_() const noexcept;

    [[nodiscard]] std::optional<std::uint8_t> direct_video_resolved_() const noexcept;

    Wiring w_;
    TastyArgs args_;
    const Run& run_;
    os::MonotonicClock clock_{};
    TastySplash splash_{};
    Stage stage_ = Stage::HideOsd;
    std::chrono::nanoseconds stage_ns_{};
    std::chrono::nanoseconds last_status_ns_{};
    app::PathText core_path_{};
    app::PathText menu_path_{};

    StatusText last_status_{};
    bool play_started_ = false;
    bool rec_seen_ = false;
    bool rec_failure_said_ = false;
    bool stop_sent_ = false;
    bool ask_owed_ = false;
    IReplayPort::SettingNo settings_printed_{};
    std::uint8_t direct_video_was_ = 0;
    bool direct_video_line_ = false;
    bool resampled_ = false;

    std::optional<app::ControlGeneration> said_gen_{};
    app::StepCount said_steps_{};
    app::StepCount said_recovered_{};
    unsigned hide_tries_ = 0;
    std::uint32_t id_gen_ = 0;
    std::uint32_t video_edges_ = 0;
    cores::MovieSystem sys_{};
};

}
