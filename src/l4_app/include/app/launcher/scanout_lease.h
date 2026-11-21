// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "app/launcher/launcher_fb_ack.h"
#include "app/launcher_state.h"
#include "app/launcher/scanout_channel.h"
#include "app/launcher/uio_burst.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "infra/tally.h"
#include "os/raw_monotonic_clock.h"
#include "svc/types.h"

namespace mister::app::launcher {

class ScanoutLease {
    TASTY_SEAT_RESIDENT(Launcher);

public:
    struct Wiring {
        const LauncherStateCell& state;
        const LauncherFbAckCell& fb_ack;
        ScanoutChannel::Owner scanout;
        std::chrono::milliseconds hold_limit;
    };

    struct Tally {
        infra::Tally leases{};
        infra::Tally refusals{};
        infra::Tally bursts{};
        infra::Tally invalid_packets{};
    };

    explicit ScanoutLease(Wiring w) noexcept : w_(std::move(w)) {}

    void offer(UniqueFd socket) noexcept;

    void serve(os::RawMonotonicClock::time_point now) noexcept;

    void settled(os::RawMonotonicClock::time_point now) noexcept;
    void close() noexcept;

    [[nodiscard]] bool open() const noexcept { return socket_.valid(); }

    [[nodiscard]] bool routed() const noexcept { return grant_by_.has_value() || granted_; }

    [[nodiscard]] bool quiescent() const noexcept { return !open() && !burst_in_flight_; }

    [[nodiscard]] bool returns_pending() const noexcept { return w_.scanout.returns_pending(); }

    [[nodiscard]] bool holding() const noexcept { return held_ask_.has_value(); }

    [[nodiscard]] bool awaiting_hold() const noexcept { return grant_by_.has_value(); }

    [[nodiscard]] std::optional<os::RawMonotonicClock::time_point> due() const noexcept {
        return grant_by_;
    }

    [[nodiscard]] const Tally& tally() const noexcept { return n_; }

    [[nodiscard]] const Tally& tally_counts() const noexcept { return n_; }

private:
    void grant_(os::RawMonotonicClock::time_point now) noexcept;
    void finish_grant_(os::RawMonotonicClock::time_point now) noexcept;
    void on_packet_(
        std::span<const std::byte> packet,
        os::RawMonotonicClock::time_point now
    ) noexcept;

    [[nodiscard]] bool post_(const UioBurst& ask) noexcept;
    void deliver_answers_() noexcept;

    Wiring w_;
    UniqueFd socket_;
    bool granted_ = false;
    bool request_waiting_ = false;
    bool burst_in_flight_ = false;
    std::optional<UioBurst> held_ask_{};
    std::optional<os::RawMonotonicClock::time_point> grant_by_{};
    Tally n_{};
};

}
