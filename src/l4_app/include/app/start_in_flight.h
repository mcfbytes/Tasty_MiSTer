// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/load_request.h"
#include "infra/seat.h"
#include "proto/link_op.h"
#include "proto/types.h"

namespace mister::app {

class StartInFlight {
public:
    TASTY_SEAT_EXEMPT(component);
    void arm(const proto::LinkOp::ApplyCore& a) noexcept {
        gen_ = a.gen;
        on_ = true;
    }
    void arm_at_boot() noexcept {
        gen_ = proto::BindGeneration{kBootGen};
        on_ = true;
    }
    void end(const proto::LinkOp::AbortSwitch&) noexcept { on_ = false; }
    void end(const proto::LinkOp::SessionUp&) noexcept { on_ = false; }
    void end(const proto::LinkOp::RebootNow&) noexcept { on_ = false; }
    void end(const proto::LinkOp::DropCore&) noexcept { on_ = false; }
    [[nodiscard]] bool on() const noexcept { return on_; }
    [[nodiscard]] proto::BindGeneration gen() const noexcept { return gen_; }

private:
    proto::BindGeneration gen_{};
    bool on_ = false;
};

}  // namespace mister::app
