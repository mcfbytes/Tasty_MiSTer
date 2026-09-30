// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "hal/core_signals.h"
#include "proto/reset_fence.h"

namespace mister::app {

class FencedCoreSignals final : public hal::ICoreSignals {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit FencedCoreSignals(hal::ICoreSignals& inner) noexcept : inner_(&inner) {}

    void attach_fence(proto::IResetFence& fence) noexcept { fence_ = &fence; }

    [[nodiscard]] Ex<hal::CoreIdentity> identify() override {
        TASTY_SEAT_BODY(FencedCoreSignals);
        return inner_->identify();
    }
    hal::CoreCapabilities capabilities() const override {
        TASTY_SEAT_BODY(FencedCoreSignals);
        return inner_->capabilities();
    }
    void set_core_reset(bool asserted) override {
        TASTY_SEAT_BODY(FencedCoreSignals);
        if (fence_ != nullptr) fence_->before_reset_line();
        inner_->set_core_reset(asserted);
    }
    void clear_gpo() override {
        TASTY_SEAT_BODY(FencedCoreSignals);
        inner_->clear_gpo();
    }

private:
    hal::ICoreSignals* inner_;
    proto::IResetFence* fence_ = nullptr;
};

}  // namespace mister::app
