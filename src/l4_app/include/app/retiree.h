// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/built_core.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/link_op.h"

namespace mister::svc {
class IStorageLifecycle;
}

namespace mister::app {

struct Retiree {
    TASTY_SEAT_EXEMPT(component);
    std::uint8_t slot = proto::LinkOp::AdoptCore::kNoSlot;
    std::uint16_t gen = 0;
    infra::OptRef<svc::IStorageLifecycle> seat{};
    std::uint16_t mask = 0;
    std::uint16_t sent = 0;

    std::array<std::uint16_t, kCoreBaySlots> merged_gen{};

    void merge(const Retiree& from) noexcept {
        mask = static_cast<std::uint16_t>(mask | from.mask);
        sent = static_cast<std::uint16_t>(sent & from.sent);
        const auto from_gens = from.bay_gens();
        for (std::size_t i = 0; i < kCoreBaySlots; ++i)
            if (from_gens[i] != 0)
                merged_gen[i] = from_gens[i];
    }

    [[nodiscard]] std::array<std::uint16_t, kCoreBaySlots> bay_gens() const noexcept {
        std::array<std::uint16_t, kCoreBaySlots> out = merged_gen;
        if (slot < kCoreBaySlots && gen != 0)
            out[slot] = gen;
        return out;
    }
};

}
