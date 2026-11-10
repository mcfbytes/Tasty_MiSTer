// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/board_ops.h"
#include "app/boot_args.h"
#include "app/config_pair.h"
#include "app/load_request.h"
#include "app/stdout_paths.h"
#include "app/stdout_router.h"
#include "app/ui_request.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
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

    [[nodiscard]] Ex<LoadRequest> argv_request(const BootArgs& args) const;

    [[nodiscard]] UiRequest::LoadCore program(const LoadRequest& req);

    [[nodiscard]] ExitReboot handoff_to_alternate_executable(const char* exe, const BootArgs& args);

    [[nodiscard]] Ex<void> re_exec(const LoadRequest& req);

    void hold_for_reboot();

    void enable_stdout_routing(std::optional<StdoutPaths> paths = std::nullopt) noexcept {
        stdout_armed_ = true;
        if (paths)
            stdout_router_.set_paths(*paths);
    }

    [[nodiscard]] infra::OptRef<StdoutRouter> boot_stdout_router() noexcept;

    [[nodiscard]] bool stdout_armed() const noexcept override {
        TASTY_SEAT_BODY(BootArm);
        return stdout_armed_;
    }

    [[nodiscard]] Ex<void> route_stdout(std::uint8_t debug) override;

    [[nodiscard]] StdoutRouter& stdout_router() noexcept { return stdout_router_; }

private:
    void sync_and_hold_();
    [[nodiscard]] ExitReboot handoff_exec_(const char* exe, const BootArgs& args);
    [[nodiscard]] Ex<void> re_exec_(const LoadRequest& req);

    const SessionMachine& machine_;
    FencedCoreSignals& signals_;
    bool stdout_armed_ = false;
    StdoutRouter stdout_router_{};
};

}
