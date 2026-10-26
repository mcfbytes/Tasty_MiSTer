// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "app/fallback_counts.h"
#include "app/pending_load.h"
#include "app/ui_request.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "os/clock.h"
#include "os/deadline.h"

namespace mister::app {

class StartWatch final {
    TASTY_SEAT_RESIDENT(Main);

public:
    using Tally = FallbackCounts;
    static constexpr std::uint32_t kStartBoundMs = 5000;

    explicit StartWatch(const os::IClock& clock) noexcept : clock_(clock) {}

    void set_identity_poll_budget(std::uint32_t n) noexcept {
        TASTY_SEAT_BODY(StartWatch);
        identity_poll_budget_ = n;
    }

    void set_start_bound_ms(std::uint32_t ms) noexcept {
        TASTY_SEAT_BODY(StartWatch);
        start_bound_ms_ = ms;
    }

    void on_seats_live() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        before_seats_ = false;
    }

    [[nodiscard]] bool bound_expired() const noexcept {
        TASTY_SEAT_BODY(StartWatch);
        if (!start_bounded_ || start_bound_ms_ == 0)
            return false;
        return start_due_.expired(clock_);
    }

    void give_up(Errc why) noexcept {
        TASTY_SEAT_BODY(StartWatch);
        awaiting_identity_ = false;
        identity_tries_ = 0;
        if (why == Errc::timeout) {
            ++fallbacks_.start_timeouts;
            fallback_cell_.publish(fallbacks_);
        }
    }

    void begin_refusal() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        awaiting_conf_str_ = false;
        start_bounded_ = false;
        start_refused_ = true;
    }

    void end_by_bridge(std::uint32_t gen) noexcept {
        TASTY_SEAT_BODY(StartWatch);
        begin_refusal();
        bridge_ended_gen_ = gen;
    }

    [[nodiscard]] bool ended_by_bridge(std::uint32_t gen) const noexcept {
        TASTY_SEAT_BODY(StartWatch);
        return start_refused_ && bridge_ended_gen_ == gen;
    }

    void owe_front_end(const UiRequest::LoadCore& pending) noexcept {
        TASTY_SEAT_BODY(StartWatch);
        if (front_end_fallback_armed_) {
            front_end_owed_ = true;
            return;
        }
        if (names_front_end_image(pending.path.view(), pending.xml)) {
            ++fallbacks_.front_end_failed;
            fallback_cell_.publish(fallbacks_);
        }
    }

    void owe_front_end_always(const UiRequest::LoadCore& pending) noexcept {
        TASTY_SEAT_BODY(StartWatch);
        if (!names_front_end_image(pending.path.view(), pending.xml)) {
            front_end_owed_ = true;
            return;
        }
        ++fallbacks_.front_end_failed;
        fallback_cell_.publish(fallbacks_);
    }

    [[nodiscard]] bool front_end_askable(bool switch_busy) const noexcept {
        TASTY_SEAT_BODY(StartWatch);
        return front_end_owed_ && !before_seats_ && !switch_busy;
    }

    void drop_front_end() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        front_end_owed_ = false;
    }

    void front_end_asked() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        ++fallbacks_.front_end_asked;
    }

    void front_end_invalid() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        front_end_owed_ = false;
        ++fallbacks_.front_end_failed;
    }

    void publish_fallbacks() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        fallback_cell_.publish(fallbacks_);
    }

    void supersede(const UiRequest::LoadCore& req) noexcept {
        TASTY_SEAT_BODY(StartWatch);
        front_end_owed_ = false;
        front_end_fallback_armed_ = !names_front_end_image(req.path.view(), req.xml);
        start_bounded_ = false;
        start_refused_ = false;
    }

    void drop_conf_str_wait() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        awaiting_conf_str_ = false;
    }

    void arm_boot_fallback(const UiRequest::LoadCore& req) noexcept {
        TASTY_SEAT_BODY(StartWatch);
        front_end_fallback_armed_ = !req.path.empty()
            && !names_front_end_image(req.path.view(), req.xml);
        before_seats_ = true;
        start_refused_ = false;
    }

    void expect_conf_str(bool loaded) noexcept {
        TASTY_SEAT_BODY(StartWatch);
        awaiting_conf_str_ = loaded;
    }

    void expect_boot_negotiate() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        front_end_fallback_armed_ = true;
        before_seats_ = true;
        start_refused_ = false;
        awaiting_conf_str_ = true;
    }

    void arm_bound() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        start_bounded_ = true;
        start_due_ = os::Deadline::in(clock_, std::chrono::milliseconds{start_bound_ms_});
    }

    void no_identity_wait() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        awaiting_identity_ = false;
    }

    void await_identity() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        identity_tries_ = 0;
        awaiting_identity_ = true;
    }

    void rebound() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        if (start_bounded_)
            start_due_ = os::Deadline::in(clock_, std::chrono::milliseconds{start_bound_ms_});
    }

    void end_bound() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        start_bounded_ = false;
    }

    [[nodiscard]] bool take_identity() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        if (!awaiting_identity_)
            return false;
        awaiting_identity_ = false;
        identity_tries_ = 0;
        return true;
    }

    void await_conf_str() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        awaiting_conf_str_ = true;
    }

    [[nodiscard]] bool accept_refusal(bool at_accept) const noexcept {
        TASTY_SEAT_BODY(StartWatch);
        return at_accept && !awaiting_identity_ && awaiting_conf_str_;
    }

    void wedged() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        awaiting_conf_str_ = false;
        start_bounded_ = false;
    }

    [[nodiscard]] bool identity_budget_spent() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        return awaiting_identity_
            && identity_poll_budget_ != 0
            && ++identity_tries_ > identity_poll_budget_;
    }

    [[nodiscard]] bool take_conf_str() noexcept {
        TASTY_SEAT_BODY(StartWatch);
        const bool awaited = awaiting_conf_str_;
        awaiting_conf_str_ = false;
        return awaited;
    }

    [[nodiscard]] bool refused() const noexcept {
        TASTY_SEAT_BODY(StartWatch);
        return start_refused_;
    }

    [[nodiscard]] bool awaiting_conf_str() const noexcept {
        TASTY_SEAT_BODY(StartWatch);
        return awaiting_conf_str_;
    }

    [[nodiscard]] bool before_seats() const noexcept {
        TASTY_SEAT_BODY(StartWatch);
        return before_seats_;
    }

    [[nodiscard]] bool front_end_owed() const noexcept {
        TASTY_SEAT_BODY(StartWatch);
        return front_end_owed_;
    }

    [[nodiscard]] std::optional<os::Deadline> bound_deadline() const noexcept {
        TASTY_SEAT_BODY(StartWatch);
        if (start_bounded_ && start_bound_ms_ != 0)
            return start_due_;
        return std::nullopt;
    }

    [[nodiscard]] const Tally& tally() const noexcept {
        TASTY_SEAT_BODY(StartWatch);
        return fallbacks_;
    }

    [[nodiscard]] const xthread::Telemetry<FallbackCounts, SeatTag::Main>&
    fallback_cell() const noexcept {
        return fallback_cell_;
    }

private:
    const os::IClock& clock_;
    bool awaiting_identity_ = false;
    bool awaiting_conf_str_ = false;
    std::uint32_t identity_poll_budget_ = 0;
    std::uint32_t identity_tries_ = 0;
    std::uint32_t start_bound_ms_ = kStartBoundMs;
    os::Deadline start_due_ = os::Deadline::immediate();
    bool start_bounded_ = false;
    bool start_refused_ = false;
    std::optional<std::uint32_t> bridge_ended_gen_{};
    bool front_end_fallback_armed_ = false;
    bool before_seats_ = false;
    bool front_end_owed_ = false;
    FallbackCounts fallbacks_{};
    xthread::Telemetry<FallbackCounts, SeatTag::Main> fallback_cell_{};
};

}
