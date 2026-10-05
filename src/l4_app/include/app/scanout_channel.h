// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>

#include "app/uio_burst.h"
#include "infra/inbox.h"
#include "infra/seat.h"
#include "infra/spsc_ring.h"
#include "infra/wake_flag.h"

namespace mister::app {

inline constexpr std::size_t kScanoutSlots = 2;

class ScanoutChannel {
    TASTY_SEAT_MEDIATOR(Launcher, RT);

public:
    explicit ScanoutChannel(xthread::WakeFlag& asker_wake) noexcept : answers_(asker_wake) {}

    explicit ScanoutChannel(xthread::Polled p) noexcept : answers_(p) {}
    ScanoutChannel(const ScanoutChannel&) = delete;
    ScanoutChannel& operator=(const ScanoutChannel&) = delete;

    [[nodiscard]] bool post(const UioBurst& b) noexcept { return asks_.push(b); }
    [[nodiscard]] std::optional<UioBurst> take_answer() noexcept { return answers_.pop(); }
    [[nodiscard]] bool answers_empty() const noexcept { return answers_.empty(); }

    [[nodiscard]] std::optional<UioBurst> take_ask() noexcept { return asks_.pop(); }
    [[nodiscard]] bool asks_empty() const noexcept { return asks_.size() == 0; }
    [[nodiscard]] bool answer(const UioBurst& b) noexcept { return answers_.push(b); }

private:
    xthread::SpscRing<UioBurst, kScanoutSlots> asks_;
    xthread::Inbox<UioBurst, kScanoutSlots> answers_;
};

}  // namespace mister::app
