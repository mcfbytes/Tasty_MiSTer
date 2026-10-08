// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "hal/core_signals.h"
#include "proto/reset_fence.h"

namespace mister::app {

class FencedCoreSignals final : public hal::ICoreSignals {
    TASTY_SEAT_RESIDENT(RT);

public:
    FencedCoreSignals(hal::ICoreSignals& inner, proto::IResetFence& fence) noexcept
        : inner_(inner), fence_(fence) {}

    [[nodiscard]] Ex<hal::CoreIdentity> identify() override {
        TASTY_SEAT_BODY(FencedCoreSignals);
        return inner_.identify();
    }
    hal::CoreCapabilities latch_capabilities() override {
        TASTY_SEAT_BODY(FencedCoreSignals);
        return inner_.latch_capabilities();
    }
    void set_core_reset(bool asserted) override {
        TASTY_SEAT_BODY(FencedCoreSignals);
        fence_.before_reset_line();
        inner_.set_core_reset(asserted);
    }
    void clear_gpo() override {
        TASTY_SEAT_BODY(FencedCoreSignals);
        inner_.clear_gpo();
    }

private:
    hal::ICoreSignals& inner_;
    proto::IResetFence& fence_;
};

}  // namespace mister::app
