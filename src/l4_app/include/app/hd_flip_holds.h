// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <optional>
#include <utility>

#include "app/canvas_gen.h"
#include "app/flip_buffer.h"
#include "app/hd_flip_slot.h"
#include "infra/indexed_array.h"
#include "infra/pool_slot.h"
#include "infra/seat.h"
#include "os/raw_monotonic_clock.h"

namespace mister::app {

class HdFlipHolds {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    using Instant = os::RawMonotonicClock::time_point;
    using Span = os::RawMonotonicClock::duration;

    static constexpr Span kLatch = std::chrono::milliseconds{50};

    explicit HdFlipHolds(HdFlipChannel& flips) noexcept : flips_(flips) {}

    HdFlipHolds(const HdFlipHolds&) = delete;
    HdFlipHolds& operator=(const HdFlipHolds&) = delete;
    HdFlipHolds(HdFlipHolds&&) = delete;
    HdFlipHolds& operator=(HdFlipHolds&&) = delete;

    void collect(Instant now) noexcept {
        for (;;) {
            HdFlipChannel::Loan loan = flips_.reap();
            if (!loan)
                break;
            if (auto back = holds_.find(xthread::index_as<FlipBuffer>(loan.slot()))) {
                back->loan = std::move(loan);
                back->free_at = now + kLatch;
            }
        }
        for (;;) {
            HdFlipChannel::Loan loan = flips_.acquire();
            if (!loan)
                break;
            if (auto back = holds_.find(xthread::index_as<FlipBuffer>(loan.slot()))) {
                back->loan = std::move(loan);
                back->free_at = Instant{};
            }
        }
    }

    void release_all() noexcept {
        for (FlipHold& h : holds_)
            h.loan = {};
        last_sent_.reset();
        while (HdFlipChannel::Loan loan = flips_.reap()) {
        }
    }

    void new_epoch() noexcept {
        last_sent_.reset();
        for (FlipHold& h : holds_)
            h.gen = {};
    }

    [[nodiscard]] bool lent(FlipBuffer b) const noexcept { return !holds_[b].loan; }

    [[nodiscard]] bool owned(FlipBuffer b, Instant now) const noexcept {
        return holds_[b].loan && now >= holds_[b].free_at;
    }

    [[nodiscard]] std::optional<Instant> latched_until(FlipBuffer b, Instant now) const noexcept {
        TASTY_SEAT_BODY(HdFlipHolds);
        if (!holds_[b].loan || now >= holds_[b].free_at)
            return std::nullopt;
        return holds_[b].free_at;
    }

    [[nodiscard]] std::optional<FlipBuffer> last_sent() const noexcept { return last_sent_; }

    [[nodiscard]] CanvasGen gen(FlipBuffer b) const noexcept { return holds_[b].gen; }

    void set_gen(FlipBuffer b, CanvasGen g) noexcept { holds_[b].gen = g; }

    [[nodiscard]] bool lend(FlipBuffer b, Instant now, const HdFlipSlot& payload) noexcept {
        if (!owned(b, now))
            return false;
        HdFlipChannel::Loan loan = std::move(holds_[b].loan);
        *loan = payload;
        flips_.send(std::move(loan));
        last_sent_ = b;
        return true;
    }

    [[nodiscard]] bool returning() const noexcept { return flips_.returns_pending(); }

private:
    struct FlipHold {
        HdFlipChannel::Loan loan{};
        Instant free_at{};
        CanvasGen gen{};
    };

    HdFlipChannel& flips_;
    infra::IndexedArray<FlipHold, 2, FlipBuffer> holds_{};
    std::optional<FlipBuffer> last_sent_{};
};

}
