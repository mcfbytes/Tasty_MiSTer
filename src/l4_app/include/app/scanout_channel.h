// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <utility>

#include "app/uio_burst.h"
#include "infra/inbox.h"
#include "infra/seat.h"
#include "infra/spsc_ring.h"
#include "infra/wake_flag.h"

namespace mister::app {

inline constexpr std::size_t kScanoutSlots = 2;

class ScanoutChannel {
    TASTY_SEAT_MEDIATOR(Launcher, RT);

    using Asks = xthread::SpscRing<UioBurst, kScanoutSlots>;
    using Answers = xthread::Inbox<UioBurst, kScanoutSlots>;

public:
    class Asker {
        TASTY_SEAT_RESIDENT(Launcher);

    public:
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(asks_); }

        [[nodiscard]] bool post(const UioBurst& b) noexcept { return asks_.push(b); }

        [[nodiscard]] std::optional<UioBurst> take_answer() noexcept { return answers_.pop(); }

        [[nodiscard]] bool answers_empty() const noexcept { return answers_.empty(); }

        [[nodiscard]] bool asks_empty() const noexcept { return asks_.size() == 0; }

    private:
        friend class ScanoutChannel;

        Asker(Asks::Producer asks, Answers& answers) noexcept
            : asks_(std::move(asks)),
              answers_(answers) {}

        Asks::Producer asks_;
        Answers& answers_;
    };

    class Answerer {
        TASTY_SEAT_RESIDENT(RT);

    public:
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(asks_); }

        [[nodiscard]] std::optional<UioBurst> take_ask() noexcept { return asks_.pop(); }

        [[nodiscard]] bool answer(const UioBurst& b) noexcept { return answers_.push(b); }

    private:
        friend class ScanoutChannel;

        Answerer(Asks::Consumer asks, Answers& answers) noexcept
            : asks_(std::move(asks)),
              answers_(answers) {}

        Asks::Consumer asks_;
        Answers& answers_;
    };

    explicit ScanoutChannel(xthread::WakeFlag& asker_wake) noexcept : answers_(asker_wake) {}

    explicit ScanoutChannel(xthread::Polled p) noexcept : answers_(p) {}

    ScanoutChannel(const ScanoutChannel&) = delete;
    ScanoutChannel& operator=(const ScanoutChannel&) = delete;

    [[nodiscard]] Asker asker() noexcept { return Asker{asks_.producer(), answers_}; }

    [[nodiscard]] Answerer answerer() noexcept { return Answerer{asks_.consumer(), answers_}; }

private:
    Asks asks_;
    Answers answers_;
};

}
