// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "launcher_block.h"
#include "os/process_title.h"
#include "process_standup.h"
#include "session_mode.h"

namespace mister::fw {

class MenuMode final : public SessionMode {
    TASTY_SEAT_EXEMPT(boot);

public:
    MenuMode(int argc, char** argv) noexcept;
    MenuMode(const MenuMode&) = delete;
    MenuMode& operator=(const MenuMode&) = delete;

    [[nodiscard]] std::unique_ptr<Front> front(const BootParts& plat) override;
    [[nodiscard]] std::unique_ptr<Tail> tail(
        const BootParts& plat,
        DeviceBlock& device,
        ReplayBlock& replay,
        RecorderBlock& rec
    ) override;

    [[nodiscard]] Traits traits() const noexcept override {
        return Traits{
            .program = "mister",
            .windows_notice = hal::FallbackNotice::Announce,
            .boot_args = {boot_argv_.data(), boot_argc_},
            .stop_ends_ladder = false,
            .not_ready_quits_clean = true
        };
    }

    [[nodiscard]] const Voice& voice() const noexcept override { return voice_; }

    void title_process() noexcept override {}

    [[nodiscard]] Exit prepare() override { return {}; }

    [[nodiscard]] int refused(const Error& e, Refusal r) override;

    void arm_stop_signals() noexcept override { stop_signals_.emplace(); }

    void disarm_stop_signals() noexcept override;
    void storage_ready(svc::Vfs& vfs) override;
    void before_boot(app::LinkSession& session) override;
    [[nodiscard]] infra::OptRef<const std::string> boot_handoff(
        const app::SessionOwner& owner
    ) const noexcept override;

    [[nodiscard]] int exit_status(const Ex<void>& ran) const noexcept override {
        return ran ? 0 : 1;
    }

private:
    class Launcher final : public Front {
        TASTY_SEAT_EXEMPT(boot);

    public:
        Launcher(const BootParts& plat, infra::OptRef<os::ProcessTitle> title)
            : launcher_{plat},
              title_{title} {}

        [[nodiscard]] DeviceBlock::Front device_front() noexcept override {
            return launcher_.device_front(true);
        }

        [[nodiscard]] infra::OptRef<app::ScanoutRelay> scanout() noexcept override {
            return infra::OptRef<app::ScanoutRelay>{launcher_.scanout_relay_};
        }

        [[nodiscard]] std::unique_ptr<Face> face(
            const BootParts& plat,
            DeviceBlock& device
        ) override;
        void latch(DeviceBlock& device, ThreadAssembly::SeatMains& mains) noexcept override;

    private:
        LauncherBlock launcher_;
        infra::OptRef<os::ProcessTitle> title_;
    };

    class StderrVoice final : public Voice {
        TASTY_SEAT_EXEMPT(boot);

    public:
        void board(const hal::BoardProfile& board, const hal::CompatibleBlob& blob) const override;
        void unknown_board(const hal::CompatibleBlob& blob) const override;
        void irq_pins(const hal::IrqPinReport& pinned) const override;
        void kernel(os::KernelRt kernel) const override;
        void fabric_not_ready(const hal::BoardProfile& board) const override;
        void wedged(std::string_view line) const noexcept override;
        void unconfigured(const app::LinkSession& session) const override;
    };

    class NoTail final : public Tail {
        TASTY_SEAT_EXEMPT(boot);

    public:
        [[nodiscard]] infra::OptRef<app::IOwnerTick> owner_tick() noexcept override { return {}; }
    };

    StderrVoice voice_{};
    Ex<os::ProcessTitle> title_;
    std::array<const char*, 3> boot_argv_{};
    std::size_t boot_argc_ = 2;

    std::optional<ProcessStandup::StopSignals> stop_signals_{};
};

}
