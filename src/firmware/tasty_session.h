// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "app/owner_tick.h"
#include "app/encode_status.h"
#include "app/path_text.h"
#include "app/recorder_status.h"
#include "app/replay_status.h"
#include "cores/movie_system.h"
#include "hal/phys_region.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "infra/wake_flag.h"
#include "os/monotonic_clock.h"
#include "tasty_cli.h"
#include "tasty_splash.h"

namespace mister::app {
class IdentityLatch;
class LinkTxChannel;
class RecorderControl;
class ReplayFeeder;
class UiRequestRing;
class VideoPump;
}  // namespace mister::app

namespace mister::svc {
class Vfs;
}

namespace mister::fw {

class TastySession final : public app::IOwnerTick {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct PlayAsk {
        std::string_view movie{};
        std::string_view rom{};
        std::optional<std::int32_t> lead{};
        std::optional<std::uint32_t> phase_us{};
        std::optional<std::uint32_t> stop_at{};
        std::optional<cores::IMovieCodec::RamFill> ram_fill{};
        bool seeded_save = false;
        bool set_settings = true;
    };

    class Replay {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        virtual ~Replay() = default;
        virtual bool take_play(const PlayAsk& ask) noexcept = 0;
        virtual void take_stop() noexcept = 0;
        [[nodiscard]] virtual std::uint32_t frames() const noexcept = 0;
        [[nodiscard]] virtual std::uint32_t movie_frames() const noexcept { return frames(); }
        [[nodiscard]] virtual bool refused() const noexcept { return false; }

        [[nodiscard]] virtual bool active() const noexcept { return true; }
        [[nodiscard]] virtual const char* refusal_why() const noexcept { return "none"; }
        [[nodiscard]] virtual std::string_view refusal_setting() const noexcept { return {}; }
        [[nodiscard]] virtual bool refusal_setting_offered() const noexcept { return false; }
        [[nodiscard]] virtual std::string_view refusal_path() const noexcept { return {}; }

        [[nodiscard]] virtual std::size_t settings_set() const noexcept { return 0; }
        [[nodiscard]] virtual std::string setting_line(std::size_t i) const {
            (void)i;
            return {};
        }
    };

    struct Wiring {
        app::VideoPump* video = nullptr;
        Replay* replay = nullptr;
        app::RecorderControl* rec = nullptr;
        app::UiRequestRing* asks = nullptr;
        app::LinkTxChannel* link_tx = nullptr;
        const app::IdentityLatch* identity = nullptr;
        const app::ReplayStatusCell* play = nullptr;
        const app::RecorderStatusCell* recstat = nullptr;
        const app::EncodeStatusCell* encstat = nullptr;
        xthread::WakeFlag* main_stop = nullptr;
        const svc::Vfs* vfs = nullptr;
        const std::atomic<int>* stop = nullptr;
        std::atomic<int>* exit_code = nullptr;
        hal::PhysRegion video_fb{};

        const xthread::Telemetry<std::uint8_t, SeatTag::Unbound>* direct_video_ini = nullptr;
    };

    TastySession(const Wiring& w, const TastyArgs& args) noexcept;
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
    [[nodiscard]] bool scaler_port_stuck_() const noexcept;
    [[nodiscard]] bool rec_only_over_() noexcept;
    void ask_core_(std::string_view path) noexcept;
    [[nodiscard]] bool core_ready_() const noexcept;

    [[nodiscard]] bool video_reloaded_() const noexcept;
    [[nodiscard]] bool menu_ready_() const noexcept;
    void fail_(int rc, const char* why) noexcept;

    [[nodiscard]] bool copy_seed_() noexcept;
    static constexpr std::uint64_t kSeedMaxBytes = 1u << 20;
    void fail_refusal_() noexcept;
    void go_menu_() noexcept;
    void print_settings_() noexcept;
    [[nodiscard]] Stage after_hide_() const noexcept;

    [[nodiscard]] std::optional<std::uint8_t> direct_video_resolved_() const noexcept;

    Wiring w_;
    TastyArgs args_;
    os::MonotonicClock clock_{};
    TastySplash splash_{};
    Stage stage_ = Stage::HideOsd;
    std::int64_t stage_ns_ = 0;
    std::int64_t last_status_ns_ = 0;
    app::PathText core_path_{};
    app::PathText menu_path_{};
    char last_status_[768]{};
    unsigned last_status_n_ = 0;
    bool rec_only_ = false;
    bool splash_ok_ = false;
    bool play_started_ = false;
    bool rec_seen_ = false;
    bool stop_sent_ = false;
    std::size_t settings_printed_ = 0;
    std::uint8_t direct_video_was_ = 0;
    bool direct_video_line_ = false;
    bool resampled_ = false;

    std::uint32_t said_gen_ = 0;
    std::uint8_t said_steps_ = 0;
    std::uint8_t said_recovered_ = 0;
    unsigned hide_tries_ = 0;
    std::uint32_t id_gen_ = 0;
    std::uint32_t video_edges_ = 0;
    cores::MovieSystem sys_{};
};

}  // namespace mister::fw
