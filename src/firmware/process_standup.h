// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "app/board_ops.h"
#include "board_parts.h"
#include "boot_parts.h"
#include "rt_evidence.h"
#include "hal/board_profile.h"
#include "hal/link_port.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "reactor/executive.h"
#include "svc/vfs.h"

namespace mister::app {
class BoardReboot;
class LinkSession;
}

namespace mister::fw {

class Hub;
class SessionMode;

class ProcessStandup {
    TASTY_SEAT_EXEMPT(boot);

public:
    using Exit = std::optional<int>;

    enum class IoSeat : std::uint8_t { Stopped, Running };

    class StopSignals {
    public:
        StopSignals() noexcept;
        ~StopSignals();
        StopSignals(const StopSignals&) = delete;
        StopSignals& operator=(const StopSignals&) = delete;
        StopSignals(StopSignals&&) = delete;
        StopSignals& operator=(StopSignals&&) = delete;

        void disarm() noexcept;

    private:
        bool armed_ = true;
    };

    class ExitPath {
        TASTY_SEAT_EXEMPT(boot);

    public:
        class SessionStage {
            TASTY_SEAT_EXEMPT(boot);

        public:
            SessionStage(
                app::LinkSession& sup,
                app::BoardReboot& reboot,
                IoSeat io,
                const SessionMode& mode
            ) noexcept
                : sup_(sup),
                  reboot_(reboot),
                  io_(io),
                  mode_(mode) {}

            void reboot_at_exit() const;

            void warn_if_unconfigured() const;

        private:
            friend class ExitPath;
            std::reference_wrapper<app::LinkSession> sup_;
            std::reference_wrapper<app::BoardReboot> reboot_;
            IoSeat io_;
            std::reference_wrapper<const SessionMode> mode_;
        };

        ExitPath() noexcept = default;

        explicit ExitPath(hal::ILinkPort& link) noexcept : link_(link) {}

        ExitPath(
            hal::ILinkPort& link,
            app::LinkSession& sup,
            app::BoardReboot& reboot,
            IoSeat io,
            const SessionMode& mode
        ) noexcept
            : link_(link),
              session_(std::in_place, sup, reboot, io, mode) {}

        [[nodiscard]] int fail() const;

        [[nodiscard]] const SessionStage& session() const;

    private:
        infra::OptRef<hal::ILinkPort> link_{};
        std::optional<SessionStage> session_{};
    };

    explicit ProcessStandup(SessionMode& mode) noexcept;
    ~ProcessStandup();
    ProcessStandup(const ProcessStandup&) = delete;
    ProcessStandup& operator=(const ProcessStandup&) = delete;
    ProcessStandup(ProcessStandup&&) = delete;
    ProcessStandup& operator=(ProcessStandup&&) = delete;

    [[nodiscard]] int run();

    [[nodiscard]] Exit prelude() noexcept;
    [[nodiscard]] Exit identify_board();
    [[nodiscard]] Exit open_platform();
    [[nodiscard]] Exit gate_fabric();
    [[nodiscard]] Exit open_storage();
    [[nodiscard]] Exit assemble_hub();
    [[nodiscard]] Exit run_boot_ladder();
    [[nodiscard]] Exit spawn_seats();
    [[nodiscard]] Ex<void> supervise();
    [[nodiscard]] int shut_down(const Ex<void>& ran);

    static bool latch_stop() noexcept;

    [[nodiscard]] static const std::atomic<int>& stop_latch() noexcept;

private:
    struct MainStopSignal final : app::IStopSignal {
        bool stop_requested() const override;
    };

    SessionMode& mode_;
    RtEvidence ev_{};
    infra::OptRef<const hal::BoardProfile> board_{};
    ExitPath exit_{};
    std::optional<BoardParts> parts_{};
    std::optional<reactor::Executive> exec_{};
    std::optional<svc::Vfs> vfs_{};
    std::optional<const BootParts> boot_parts_{};
    MainStopSignal stop_signal_{};
    std::unique_ptr<Hub> hub_{};
};

}
