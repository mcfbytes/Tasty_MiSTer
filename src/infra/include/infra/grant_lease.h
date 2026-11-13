// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "infra/grant_gen.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::xthread {

template <class G, SeatTag M>
[[nodiscard]] bool grant_unchanged(const Telemetry<G, M>& grant, GrantGen held) noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return held.is_current_in(grant);
}

template <SeatTag H>
[[nodiscard]] bool held_after_revoke(const Telemetry<GrantGen, H>& lease) noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (lease.generation() == 0)
        return false;
    GrantGen body{};
    if (lease.sample_into(body) == 0)
        return true;
    return body.held();
}

template <class Grant, class Owner>
class GrantLease {
    TASTY_SEAT_FROM(Owner);

public:
    using GrantCell = Telemetry<Grant, SeatTag::Main>;
    using LeaseCell = Telemetry<GrantGen, Owner::kSeatDecl>;

    struct Taken {
        Grant grant;
        GrantGen gen;
    };

    GrantLease(const GrantCell& grant, LeaseCell& lease) noexcept : grant_(grant), lease_(lease) {}

    GrantLease(const GrantLease&) = delete;
    GrantLease& operator=(const GrantLease&) = delete;

    [[nodiscard]] std::optional<Taken> take() noexcept {
        const std::optional<Grant> g = rd_.take_if_changed(grant_);
        if (!g)
            return std::nullopt;
        return Taken{*g, GrantGen::seen_by(rd_)};
    }

    void bind(GrantGen gen) noexcept { bound_ = gen; }

    void unbind() noexcept { bound_ = GrantGen{}; }

    [[nodiscard]] bool lease() noexcept {
        lease_.publish(bound_);
        if (grant_unchanged(grant_, bound_))
            return true;
        lease_.publish(GrantGen{});
        return false;
    }

    void release() noexcept { lease_.publish(GrantGen{}); }

    [[nodiscard]] bool bound() const noexcept { return bound_.held(); }

    [[nodiscard]] bool untaken() const noexcept {
        const std::uint32_t g = grant_.generation();
        return g != 0 && g != rd_.seen();
    }

    void forget() noexcept { rd_.forget(); }

private:
    const GrantCell& grant_;
    LeaseCell& lease_;
    typename GrantCell::Reader rd_{};
    GrantGen bound_{};
};

}
