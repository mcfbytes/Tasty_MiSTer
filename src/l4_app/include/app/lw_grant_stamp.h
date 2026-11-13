// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/types.h"
#include "proto/build_generation.h"

namespace mister::app {

class LwGrantStamp {
public:
    [[nodiscard]] static constexpr LwGrantStamp claimed_on(FabricGeneration f) noexcept {
        LwGrantStamp s{};
        s.fabric_gen_ = f;
        return s;
    }

    [[nodiscard]] static constexpr LwGrantStamp built_as(
        proto::BuildGeneration g,
        FabricGeneration f
    ) noexcept {
        LwGrantStamp s{};
        s.gen_ = g;
        s.fabric_gen_ = f;
        return s;
    }

    [[nodiscard]] constexpr proto::BuildGeneration gen() const noexcept { return gen_; }

    [[nodiscard]] constexpr bool unbuilt() const noexcept { return !gen_.built(); }

    [[nodiscard]] constexpr bool names(proto::BuildGeneration g) const noexcept {
        return gen_ == g;
    }

    [[nodiscard]] constexpr bool lendable_for(proto::BuildGeneration g) const noexcept {
        return names(g) && !lost();
    }

    [[nodiscard]] constexpr bool granted_on(FabricGeneration f) const noexcept {
        return fabric_gen_ == f;
    }

    [[nodiscard]] constexpr bool lost() const noexcept { return lost_; }

    [[nodiscard]] constexpr LwGrantStamp adopted_by(proto::BuildGeneration g) const noexcept {
        LwGrantStamp s = *this;
        s.gen_ = g;
        return s;
    }

    [[nodiscard]] constexpr LwGrantStamp with_fabric_lost() const noexcept {
        LwGrantStamp s = *this;
        s.lost_ = true;
        return s;
    }

private:
    proto::BuildGeneration gen_{};
    FabricGeneration fabric_gen_{};
    bool lost_ = false;
};

}
