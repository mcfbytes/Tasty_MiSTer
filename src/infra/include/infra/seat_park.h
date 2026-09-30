// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <poll.h>

#include <cerrno>
#include <chrono>
#include <concepts>
#include <cstdint>

#include "infra/error.h"
#include "infra/park_like.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::xthread {

class SeatPark {
    TASTY_SEAT_EXEMPT(component);

public:
    SeatPark(WakeFlag& wake, const WakeFlag& stop, int extra = -1) noexcept
        : wake_(wake), stop_(stop), extra_(extra) {
        if constexpr (kSeatChecksEnabled) {
            if (wake_.fd() < 0)
                fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "SeatPark over a flag with no fd");
            if (stop_.fd() < 0)
                fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "SeatPark over a stop with no fd");
        }
    }
    SeatPark(const SeatPark&) = delete;
    SeatPark& operator=(const SeatPark&) = delete;
    SeatPark(SeatPark&&) = delete;
    SeatPark& operator=(SeatPark&&) = delete;

    void arm() noexcept { wake_.arm(); }

    void mute_extra(bool on) noexcept { extra_muted_ = on; }
    void disarm() noexcept { wake_.disarm(); }

    void wait(int ms) noexcept {
        ++waits_;
        extra_ready_ = false;
        const short extra_events = extra_muted_ ? short{0} : short{POLLIN};
        pollfd fds[3] = {
            {wake_.fd(), POLLIN, 0}, {stop_.fd(), POLLIN, 0}, {extra_, extra_events, 0}};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        int left = ms;
        int n = 0;
        for (;;) {
            n = ::poll(fds, 3, left);
            if (n >= 0 || errno != EINTR) break;
            if (ms > 0) left = remaining_ms_(deadline);
        }
        if (n == 0) {
            ++timeouts_;
            return;
        }
        if constexpr (kSeatChecksEnabled) {
            if (n < 0)
                fatal(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)},
                      "SeatPark poll");
            for (const pollfd& f : fds)
                if ((f.revents & POLLNVAL) != 0)
                    fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "SeatPark polls a closed fd");
        }
        extra_ready_ = n > 0 && (fds[2].revents & POLLIN) != 0;
        if (n > 0 && (fds[0].revents & POLLIN) != 0) {
            ++wakes_;
            wake_.drain();
        }
    }

    [[nodiscard]] std::uint32_t waits() const noexcept { return waits_; }
    [[nodiscard]] std::uint32_t wakes() const noexcept { return wakes_; }
    [[nodiscard]] std::uint32_t timeouts() const noexcept { return timeouts_; }

    [[nodiscard]] bool extra_ready() const noexcept { return extra_ready_; }

private:
    static int remaining_ms_(std::chrono::steady_clock::time_point deadline) noexcept {
        const auto left = std::chrono::ceil<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        return left.count() > 0 ? static_cast<int>(left.count()) : 0;
    }

    WakeFlag& wake_;
    const WakeFlag& stop_;
    int extra_;
    std::uint32_t waits_ = 0;
    std::uint32_t wakes_ = 0;
    std::uint32_t timeouts_ = 0;
    bool extra_ready_ = false;
    bool extra_muted_ = false;
};

static_assert(ParkLike<SeatPark>);

}  // namespace mister::xthread
