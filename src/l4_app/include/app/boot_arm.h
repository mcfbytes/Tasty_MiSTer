// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "app/board_ops.h"
#include "app/config_pair.h"
#include "app/load_request.h"
#include "app/stdout_router.h"
#include "app/ui_request.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::app {

class FencedCoreSignals;
class SessionMachine;

class BootArm final : public ConfigPair::IStdoutRoute {
    TASTY_SEAT_RESIDENT(RT);

public:
    BootArm(const SessionMachine& machine, FencedCoreSignals& signals) noexcept
        : machine_(machine),
          signals_(signals) {}

    [[nodiscard]] bool in_boot_state() const noexcept;

    [[nodiscard]] Ex<LoadRequest> argv_request(std::span<const char* const> argv) const;

    [[nodiscard]] UiRequest::LoadCore program(const LoadRequest& req);

    [[nodiscard]] ExitReboot handoff_to_alternate_executable(
        const char* exe,
        std::span<const char* const> argv
    );

    [[nodiscard]] Ex<void> re_exec(const LoadRequest& req);

    void hold_for_reboot();

    void enable_stdout_routing(
        const char* null_path = nullptr,
        const char* debug_path = nullptr
    ) noexcept {
        stdout_armed_ = true;
        if (null_path != nullptr && debug_path != nullptr) {
            stdout_router_.set_paths(null_path, debug_path);
        }
    }

    [[nodiscard]] StdoutRouter* boot_stdout_router() noexcept;

    [[nodiscard]] bool stdout_armed() const noexcept override {
        TASTY_SEAT_BODY(BootArm);
        return stdout_armed_;
    }

    [[nodiscard]] Ex<void> route_stdout(std::uint8_t debug) override;

    [[nodiscard]] StdoutRouter& stdout_router() noexcept { return stdout_router_; }

private:
    void sync_and_hold_();
    [[nodiscard]] ExitReboot handoff_exec_(const char* exe, std::span<const char* const> argv);
    [[nodiscard]] Ex<void> re_exec_(const LoadRequest& req);

    const SessionMachine& machine_;
    FencedCoreSignals& signals_;
    bool stdout_armed_ = false;
    StdoutRouter stdout_router_{};
};

}
