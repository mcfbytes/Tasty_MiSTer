// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/lease_order.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

template <class Grant, class Route, SeatTag Seat>
class LwBlockEndpoint {
public:
    using GrantCell = xthread::Telemetry<Grant, SeatTag::Main>;

    explicit LwBlockEndpoint(const GrantCell& cell) noexcept : cell_(cell) {}

    LwBlockEndpoint(const LwBlockEndpoint&) = delete;
    LwBlockEndpoint& operator=(const LwBlockEndpoint&) = delete;
    virtual ~LwBlockEndpoint() = default;

    void serve() noexcept {
        seat_assert<Seat>(ERR_SITE(), __func__);
        take_grant_();
        if (bound_gen_ == 0)
            return;
        serve_bound_();
    }

    [[nodiscard]] bool idle() const noexcept {
        if (const std::uint32_t g = cell_.generation(); g != 0 && g != grant_rd_.seen())
            return false;
        return bound_gen_ == 0 || idle_bound_hook_();
    }

    void on_pause() noexcept {
        seat_assert<Seat>(ERR_SITE(), __func__);
        unbind_();
    }

    void on_resume() noexcept {
        seat_assert<Seat>(ERR_SITE(), __func__);
        grant_rd_.forget();
    }

    void on_stop() noexcept {
        seat_assert<Seat>(ERR_SITE(), __func__);
        unbind_();
    }

    [[nodiscard]] bool bound() const noexcept { return bound_gen_ != 0; }

protected:
    [[nodiscard]] bool lease_() noexcept {
        tally_.leased = true;
        publish_();
        if (xthread::grant_unchanged(cell_, bound_gen_))
            return true;
        tally_.leased = false;
        ++tally_.revoked;
        unbind_();
        return false;
    }

    void release_() noexcept {
        tally_.leased = false;
        publish_();
    }

    void publish_() noexcept { publish_hook_(tally_); }

    Grant grant_{};
    Route tally_{};

private:
    void take_grant_() noexcept {
        const std::optional<Grant> g = grant_rd_.take_if_changed(cell_);
        if (!g)
            return;

        const std::uint32_t gen = grant_rd_.seen();

        unbind_();
        if (g->live())
            bind_(*g, gen);
    }

    void bind_(const Grant& g, std::uint32_t gen) noexcept {
        grant_ = g;
        bound_gen_ = gen;
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
        tally_.on_lw = true;
        bound_hook_();
        answer_hook_(g.gen);
    }

    void unbind_() noexcept {
        if (bound_gen_ == 0)
            return;
        unbind_hook_();
        grant_ = Grant{};
        bound_gen_ = 0;
        tally_.on_lw = false;
        publish_();
    }

    static constexpr bool kRouteCountsRefusal = requires(const Route& r) { r.bind_refusals; };

    virtual void publish_hook_(const Route& r) noexcept = 0;

    virtual void prepare_hook_(const Grant& g) noexcept = 0;

    [[nodiscard]] virtual bool arm_hook_(const Grant& g) noexcept = 0;

    virtual void bind_hook_(const Grant& g) noexcept = 0;

    virtual void bound_hook_() noexcept = 0;

    virtual void answer_hook_(std::uint16_t gen) noexcept = 0;

    virtual void unbind_hook_() noexcept = 0;
    virtual void serve_bound_() noexcept = 0;
    [[nodiscard]] virtual bool idle_bound_hook_() const noexcept = 0;

    const GrantCell& cell_;
    typename GrantCell::Reader grant_rd_{};
    std::uint32_t bound_gen_ = 0;
};

}
