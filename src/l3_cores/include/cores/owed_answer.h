// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores::detail {

template <class Act, std::uint32_t kRounds>
class OwedAnswer {
    TASTY_SEAT_RESIDENT(RT);

public:
    OwedAnswer() noexcept = default;
    OwedAnswer(const OwedAnswer&) = delete;
    OwedAnswer& operator=(const OwedAnswer&) = delete;
    OwedAnswer(OwedAnswer&&) = delete;
    OwedAnswer& operator=(OwedAnswer&&) = delete;

    ~OwedAnswer() { stranded(); }

    [[nodiscard]] bool owed() const noexcept { return static_cast<bool>(gen_); }

    [[nodiscard]] proto::ActKey generation() const noexcept { return gen_; }

    [[nodiscard]] const Act& act() const noexcept { return act_; }

    [[nodiscard]] svc::Tally stranded_drops() const noexcept { return stranded_drops_; }

    void arm(proto::ActKey generation, const Act& act) noexcept {
        if (gen_ && gen_ != generation)
            stranded();
        gen_ = generation;
        act_ = act;
        waited_rounds_ = 0;
    }

    [[nodiscard]] bool waited_past() noexcept {
        if (!gen_)
            return false;
        if (waited_rounds_ < kRounds)
            ++waited_rounds_;
        return waited_rounds_ >= kRounds;
    }

    void answered() noexcept { forget_(); }

    void declined() noexcept { forget_(); }

    void stranded() noexcept {
        if (!gen_)
            return;
        stranded_drops_.add();
        forget_();
    }

private:
    void forget_() noexcept {
        gen_ = {};
        act_ = {};
        waited_rounds_ = 0;
    }

    Act act_{};
    proto::ActKey gen_{};
    std::uint32_t waited_rounds_ = 0;
    svc::Tally stranded_drops_{};
};

}
