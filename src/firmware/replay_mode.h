// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "app/replay_feeder.h"
#include "boot_parts.h"
#include "device_block.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "recorder_block.h"
#include "replay_block.h"
#include "session_mode.h"
#include "tasty_cli.h"
#include "tasty_ctl.h"
#include "tasty_entry.h"
#include "tasty_osd.h"
#include "tasty_session.h"
#include "tasty_sink.h"

namespace mister::fw {

class ReplayMode final : public SessionMode {
    TASTY_SEAT_EXEMPT(boot);

public:
    explicit ReplayMode(const TastyArgs& args, std::optional<ArgvArea> area = std::nullopt) noexcept
        : args_(args),
          area_(area) {}

    ReplayMode(const ReplayMode&) = delete;
    ReplayMode& operator=(const ReplayMode&) = delete;

    [[nodiscard]] std::unique_ptr<Front> front(const BootParts& plat) override;
    [[nodiscard]] std::unique_ptr<Tail> tail(
        const BootParts& plat,
        DeviceBlock& device,
        ReplayBlock& replay,
        RecorderBlock& rec
    ) override;

    [[nodiscard]] Traits traits() const noexcept override {

        return Traits{
            .program = "tasty",
            .windows_notice = hal::FallbackNotice::Quiet,
            .boot_args = kBootArgv,
            .stop_ends_ladder = true,
            .not_ready_quits_clean = false
        };
    }

    [[nodiscard]] const Voice& voice() const noexcept override { return voice_; }

    void title_process() noexcept override;
    [[nodiscard]] Exit prepare() override;
    [[nodiscard]] int refused(const Error& e, Refusal r) override;

    void arm_stop_signals() noexcept override {}

    void disarm_stop_signals() noexcept override {}

    void storage_ready(svc::Vfs&) override {}

    void before_boot(app::LinkSession&) override {}

    [[nodiscard]] infra::OptRef<const std::string> boot_handoff(
        const app::SessionOwner&
    ) const noexcept override {
        return {};
    }

    [[nodiscard]] int exit_status(const Ex<void>& ran) const noexcept override;

private:
    static constexpr const char* kBootArgv[2] = {"tasty", ""};

    class QuietVoice final : public Voice {
        TASTY_SEAT_EXEMPT(boot);

    public:
        explicit QuietVoice(const TastyArgs& args) noexcept : args_(args) {}

        void board(const hal::BoardProfile& board, const hal::CompatibleBlob& blob) const override;
        void unknown_board(const hal::CompatibleBlob& blob) const override;

        void irq_pins(const hal::IrqPinReport&) const override {}

        void kernel(os::KernelRt) const override {}

        void fabric_not_ready(const hal::BoardProfile& board) const override;
        void wedged(std::string_view line) const noexcept override;

        void unconfigured(const app::LinkSession&) const override {}

    private:
        const TastyArgs& args_;
    };

    struct FeederReplay final : TastySession::Replay {
        TASTY_SEAT_RESIDENT(Ui);

        explicit FeederReplay(app::ReplayFeeder& feeder) noexcept : f(feeder) {}

        app::ReplayFeeder& f;
        bool take_play(const TastySession::PlayAsk& ask) noexcept override;
        void take_stop() noexcept override;
        [[nodiscard]] std::uint32_t frames() const noexcept override;
        [[nodiscard]] std::uint32_t movie_frames() const noexcept override;
        [[nodiscard]] bool refused() const noexcept override;
        [[nodiscard]] bool active() const noexcept override;
        [[nodiscard]] const char* refusal_why() const noexcept override;
        [[nodiscard]] std::string_view refusal_setting() const noexcept override;
        [[nodiscard]] bool refusal_setting_offered() const noexcept override;
        [[nodiscard]] std::string_view refusal_path() const noexcept override;
        [[nodiscard]] std::size_t settings_set() const noexcept override;
        [[nodiscard]] std::string setting_line(std::size_t i) const override;
    };

    class NoFront final : public Front {
        TASTY_SEAT_EXEMPT(boot);

    public:
        [[nodiscard]] DeviceBlock::Front device_front() noexcept override { return {}; }

        [[nodiscard]] infra::OptRef<app::ScanoutRelay> scanout() noexcept override { return {}; }

        [[nodiscard]] std::unique_ptr<Face> face(
            const BootParts& plat,
            DeviceBlock& device
        ) override;

        void latch(DeviceBlock&, ThreadAssembly::SeatMains&) noexcept override {}
    };

    class Sink final : public Face {
        TASTY_SEAT_EXEMPT(boot);

    public:
        explicit Sink(DeviceBlock& device) noexcept
            : tasty_sink_{device.video_pump_, &device.session_.identity()} {}

        [[nodiscard]] Parts parts() noexcept override {
            return Parts{.osd_close = null_osd_, .ui_sink = tasty_sink_};
        }

        void open() noexcept override {}

        void route_pages_to(app::ICmdVerbSink&) noexcept override {}

        void latch(const svc::ConfigSnapshot&, ThreadAssembly::SeatMains&) noexcept override {}

    private:
        NullOsdClose null_osd_;
        TastySink tasty_sink_;
    };

    class Owner final : public Tail {
        TASTY_SEAT_EXEMPT(boot);

    public:
        Owner(
            const BootParts& plat,
            DeviceBlock& device,
            ReplayBlock& replay,
            RecorderBlock& rec,
            const TastyArgs& args
        );

        [[nodiscard]] infra::OptRef<app::IOwnerTick> owner_tick() noexcept override {
            return infra::OptRef<app::IOwnerTick>{session_};
        }

    private:
        FeederReplay feeder_;
        TastySession session_;
    };

    TastyArgs args_;
    QuietVoice voice_{args_};
    std::optional<ArgvArea> area_;

    std::optional<ReturnHome> home_{};
};

[[nodiscard]] const std::atomic<int>& tasty_owner_stop_flag() noexcept;
void tasty_arm_owner_signals() noexcept;

void tasty_owe_home() noexcept;

[[nodiscard]] int tasty_stop_and_owe() noexcept;

[[nodiscard]] bool tasty_home_may_launch() noexcept;

}
