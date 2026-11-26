// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/block_grant.h"
#include "infra/error.h"
#include "infra/xthread/grant_lease.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "proto/build_generation.h"

namespace mister::app {

template <class Grant, class Route, class Owner>
class GrantEndpoint {
    TASTY_SEAT_FROM(Owner);

public:
    using GrantCell = xthread::Telemetry<Grant, SeatTag::Main>;
    using LeaseCell = xthread::Telemetry<infra::CellGen, Owner::kSeatDecl>;
    using Lease = xthread::GrantLease<Grant, Owner>;

    static_assert(
        Owner::kSeatDecl != SeatTag::Unbound,
        "a block endpoint's owner tag must declare a seat"
    );
    static_assert(BlockGrantRecord<Grant>, "a block endpoint takes a BlockGrant head's record");

    GrantEndpoint(const GrantCell& cell, LeaseCell& lease) noexcept : grant_lease_(cell, lease) {}

    GrantEndpoint(const GrantEndpoint&) = delete;
    GrantEndpoint& operator=(const GrantEndpoint&) = delete;
    virtual ~GrantEndpoint() = default;

    void serve() noexcept {
        seat_assert<Owner::kSeatDecl>(ERR_SITE(), __func__);
        take_grant_();
        if (!grant_lease_.bound())
            return;
        serve_bound_();
    }

    [[nodiscard]] bool idle() const noexcept {
        return !grant_lease_.untaken() && (!grant_lease_.bound() || idle_bound_hook_());
    }

    void on_pause() noexcept {
        seat_assert<Owner::kSeatDecl>(ERR_SITE(), __func__);
        unbind_();
    }

    void on_resume() noexcept {
        seat_assert<Owner::kSeatDecl>(ERR_SITE(), __func__);
        grant_lease_.forget();
    }

    void on_stop() noexcept {
        seat_assert<Owner::kSeatDecl>(ERR_SITE(), __func__);
        unbind_();
    }

    [[nodiscard]] bool bound() const noexcept { return grant_lease_.bound(); }

protected:
    [[nodiscard]] bool lease_() noexcept {
        if (grant_lease_.lease())
            return true;
        count_one_(tally_.revoked);
        unbind_();
        return false;
    }

    void release_() noexcept {
        grant_lease_.release();
        publish_();
    }

    void publish_() noexcept { publish_hook_(tally_); }

    Grant grant_{};
    Route tally_{};

    void unbind_() noexcept {
        if (!grant_lease_.bound())
            return;
        unbind_hook_();
        grant_ = Grant{};
        grant_lease_.unbind();
        tally_.bound = false;
        publish_();
    }

private:
    void take_grant_() noexcept {
        const std::optional<typename Lease::Taken> t = grant_lease_.take();
        if (!t)
            return;

        unbind_();
        if (t->grant.live() && accepts_hook_(t->grant))
            bind_(t->grant, t->gen);
    }

    void bind_(const Grant& g, infra::CellGen gen) noexcept {
        grant_ = g;
        grant_lease_.bind(gen);
        prepare_hook_(g);
        if (!lease_()) {
            answer_hook_(g.gen);
            return;
        }
        if (!arm_hook_(g)) {
            release_();
            unbind_();
            if constexpr (kRouteCountsRefusal)
                ++tally_.bind_refusals;
            answer_hook_(g.gen);
            return;
        }
        bind_hook_(g);
        release_();
        tally_.bound = true;
        bound_hook_();
        answer_hook_(g.gen);
    }

    static constexpr bool kRouteCountsRefusal = requires(const Route& r) { r.bind_refusals; };

    template <class N>
    static constexpr void count_one_(N& n) noexcept {
        if constexpr (requires { n.add(); })
            n.add();
        else
            ++n;
    }

    [[nodiscard]] virtual bool accepts_hook_(const Grant&) const noexcept { return true; }

    virtual void publish_hook_(const Route& r) noexcept = 0;

    virtual void prepare_hook_(const Grant& g) noexcept = 0;

    [[nodiscard]] virtual bool arm_hook_(const Grant& g) noexcept = 0;

    virtual void bind_hook_(const Grant& g) noexcept = 0;

    virtual void bound_hook_() noexcept = 0;

    virtual void answer_hook_(proto::BuildGeneration gen) noexcept = 0;

    virtual void unbind_hook_() noexcept = 0;
    virtual void serve_bound_() noexcept = 0;
    [[nodiscard]] virtual bool idle_bound_hook_() const noexcept = 0;

    Lease grant_lease_;
};

}
