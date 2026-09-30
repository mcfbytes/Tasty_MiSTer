// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "app/park_receipt.h"
#include "infra/inbox.h"
#include "infra/seat.h"
#include "infra/spsc_ring.h"
#include "infra/wake_flag.h"

namespace mister::app {

struct Quiesce {
    std::uint32_t gen = 0;
};
static_assert(std::is_trivially_copyable_v<Quiesce>);

struct Quiesced {
    std::uint32_t gen = 0;
};
static_assert(std::is_trivially_copyable_v<Quiesced>);
static_assert(sizeof(Quiesced) <= 8, "one cache line holds the whole handshake");

inline constexpr std::size_t kQuiesceSlots = 4;

class QuiesceChannel {
    TASTY_SEAT_MEDIATOR(Any, RT);

public:
    QuiesceChannel() noexcept : ack_(xthread::Polled{}) {}

    explicit QuiesceChannel(xthread::WakeFlag& wake) noexcept : ack_(wake) {}
    QuiesceChannel(const QuiesceChannel&) = delete;
    QuiesceChannel& operator=(const QuiesceChannel&) = delete;

    [[nodiscard]] bool post_ask(const Quiesce& q) noexcept { return ask_.push(q); }

    [[nodiscard]] bool ask_room() const noexcept { return ask_.size() < kQuiesceSlots; }

    [[nodiscard]] std::optional<ParkReceipt> take_ack() noexcept {
        const auto q = ack_.pop();
        if (!q) return std::nullopt;
        return ParkReceipt{q->gen};
    }

    [[nodiscard]] bool ack_empty() const noexcept { return ack_.size() == 0; }

    [[nodiscard]] std::optional<Quiesce> take_ask() noexcept { return ask_.pop(); }

    [[nodiscard]] bool post_ack(const Quiesced& q) noexcept { return ack_.push(q); }

private:
    xthread::SpscRing<Quiesce, kQuiesceSlots> ask_;
    xthread::Inbox<Quiesced, kQuiesceSlots> ack_;
};

}  // namespace mister::app
