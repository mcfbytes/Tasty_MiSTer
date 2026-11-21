// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "infra/cell_gen.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::infra::xthread {

template <class G, SeatTag M>
[[nodiscard]] bool grant_unchanged(const Telemetry<G, M>& grant, infra::CellGen held) noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return held.is_current_in(grant);
}

template <SeatTag H>
[[nodiscard]] bool held_after_revoke(const Telemetry<infra::CellGen, H>& lease) noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (!lease.generation().held())
        return false;
    infra::CellGen body{};
    if (lease.sample_into(body) == 0)
        return true;
    return body.held();
}

template <class Grant, class Owner>
class GrantLease {
    TASTY_SEAT_FROM(Owner);

public:
    using GrantCell = Telemetry<Grant, SeatTag::Main>;
    using LeaseCell = Telemetry<infra::CellGen, Owner::kSeatDecl>;

    struct Taken {
        Grant grant;
        infra::CellGen gen;
    };

    GrantLease(const GrantCell& grant, LeaseCell& lease) noexcept : grant_(grant), lease_(lease) {}

    GrantLease(const GrantLease&) = delete;
    GrantLease& operator=(const GrantLease&) = delete;

    [[nodiscard]] std::optional<Taken> take() noexcept {
        const std::optional<Grant> g = rd_.take_if_changed(grant_);
        if (!g)
            return std::nullopt;
        return Taken{*g, infra::CellGen::seen_by(rd_)};
    }

    void bind(infra::CellGen gen) noexcept { bound_ = gen; }

    void unbind() noexcept { bound_ = infra::CellGen{}; }

    [[nodiscard]] bool lease() noexcept {
        lease_.publish(bound_);
        if (grant_unchanged(grant_, bound_))
            return true;
        lease_.publish(infra::CellGen{});
        return false;
    }

    void release() noexcept { lease_.publish(infra::CellGen{}); }

    [[nodiscard]] bool bound() const noexcept { return bound_.held(); }

    [[nodiscard]] bool untaken() const noexcept { return rd_.unread(grant_); }

    void forget() noexcept { rd_.forget(); }

private:
    const GrantCell& grant_;
    LeaseCell& lease_;
    typename GrantCell::Reader rd_{};
    infra::CellGen bound_{};
};

}
