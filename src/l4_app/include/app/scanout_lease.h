// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "app/launcher_fb_ack.h"
#include "app/launcher_state.h"
#include "app/scanout_channel.h"
#include "app/uio_burst.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "svc/types.h"

namespace mister::app {

class ScanoutLease {
    TASTY_SEAT_RESIDENT(Launcher);

public:
    struct Wiring {
        const LauncherStateCell& state;
        const LauncherFbAckCell& fb_ack;
        ScanoutChannel::Asker& scanout;
        std::chrono::milliseconds hold_limit;
    };

    struct Tally {
        svc::Tally leases{};
        svc::Tally refusals{};
        svc::Tally bursts{};
        svc::Tally invalid_packets{};
    };

    explicit ScanoutLease(Wiring w) noexcept : w_(w) {}

    void offer(UniqueFd socket) noexcept;

    void serve(std::chrono::nanoseconds now) noexcept;

    void settled(std::chrono::nanoseconds now) noexcept;
    void close() noexcept;

    [[nodiscard]] bool open() const noexcept { return socket_.valid(); }

    [[nodiscard]] bool routed() const noexcept { return grant_by_.has_value() || granted_; }

    [[nodiscard]] bool quiescent() const noexcept { return !open() && !burst_in_flight_; }

    [[nodiscard]] bool holding() const noexcept { return held_ask_.has_value(); }

    [[nodiscard]] bool awaiting_hold() const noexcept { return grant_by_.has_value(); }

    [[nodiscard]] std::optional<std::chrono::nanoseconds> due() const noexcept { return grant_by_; }

    [[nodiscard]] const Tally& tally() const noexcept { return n_; }

    [[nodiscard]] const Tally& tally_counts() const noexcept { return n_; }

private:
    void grant_(std::chrono::nanoseconds now) noexcept;
    void finish_grant_(std::chrono::nanoseconds now) noexcept;
    void on_packet_(std::span<const std::byte> packet, std::chrono::nanoseconds now) noexcept;
    void deliver_answers_() noexcept;

    Wiring w_;
    UniqueFd socket_;
    bool granted_ = false;
    bool request_waiting_ = false;
    bool burst_in_flight_ = false;
    std::optional<UioBurst> held_ask_{};
    std::optional<std::chrono::nanoseconds> grant_by_{};
    Tally n_{};
};

}
