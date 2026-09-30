// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "infra/seat.h"
#include "infra/spsc_ring.h"
#include "infra/wake_flag.h"

namespace mister::xthread {

template <class T, std::size_t N, KickPolicy Kick = EventFdKick>
class Inbox {
    TASTY_SEAT_MEDIATOR(Any, Any);

public:
    explicit Inbox(BasicWakeFlag<Kick>& wake) noexcept : wake_(&wake) {}

    explicit Inbox(Polled) noexcept {}
    Inbox(const Inbox&) = delete;
    Inbox& operator=(const Inbox&) = delete;
    Inbox(Inbox&&) = delete;
    Inbox& operator=(Inbox&&) = delete;

    [[nodiscard]] bool push(const T& v) noexcept {
        if (!ring_.push(v)) return false;
        if (wake_ != nullptr) wake_->kick_if_armed();
        return true;
    }

    void kick_if_armed() noexcept {
        if (wake_ != nullptr) wake_->kick_if_armed();
    }
    void kick() noexcept {
        if (wake_ != nullptr) wake_->kick();
    }
    [[nodiscard]] std::optional<T> pop() noexcept { return ring_.pop(); }
    [[nodiscard]] bool empty() const noexcept { return ring_.size() == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return ring_.size(); }

    [[nodiscard]] std::uint32_t pushed() const noexcept { return ring_.pushed(); }
    [[nodiscard]] std::uint32_t popped() const noexcept { return ring_.popped(); }

private:
    BasicWakeFlag<Kick>* wake_ = nullptr;
    SpscRing<T, N> ring_{};
};

}  // namespace mister::xthread
