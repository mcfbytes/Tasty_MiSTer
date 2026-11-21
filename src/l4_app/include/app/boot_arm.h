// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/board_ops.h"
#include "app/boot_args.h"
#include "app/load_request.h"
#include "app/ui_request.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::app {

class FencedCoreSignals;
class SessionMachine;

class BootArm {
    TASTY_SEAT_RESIDENT(RT);

public:
    BootArm(const SessionMachine& machine, FencedCoreSignals& signals) noexcept
        : machine_(machine),
          signals_(signals) {}

    [[nodiscard]] bool in_boot_state() const noexcept;

    [[nodiscard]] Ex<LoadRequest> argv_request(const BootArgs& args) const;

    [[nodiscard]] UiRequest::LoadCore program(const LoadRequest& req);

    void hold_core();

private:
    const SessionMachine& machine_;
    FencedCoreSignals& signals_;
};

}
