// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "app/input_wire.h"
#include "infra/diag_log.h"
#include "infra/seat.h"
#include "proto/link_event.h"
#include "svc/rumble_out.h"

namespace mister::app {

class RumbleRelay {
    TASTY_SEAT_RESIDENT(Input);

public:
    RumbleRelay(svc::IRumbleOut& out, RumblePadsCell& pads, xthread::DiagLog& diag) noexcept
        : out_(out),
          pads_(pads),
          diag_(diag) {}

    RumbleRelay(const RumbleRelay&) = delete;
    RumbleRelay& operator=(const RumbleRelay&) = delete;

    void refresh() noexcept;

    void on(const proto::LinkEvent::PolledWord& w) noexcept;

    [[nodiscard]] std::uint16_t last_applied(std::uint8_t player) const noexcept {
        return player < last_.size() ? last_[player] : std::uint16_t{0};
    }

    [[nodiscard]] std::uint32_t applied() const noexcept { return applied_; }

    [[nodiscard]] std::uint32_t repeats() const noexcept { return repeats_; }

    [[nodiscard]] std::uint32_t no_device() const noexcept { return no_device_; }

    [[nodiscard]] std::uint32_t failures() const noexcept { return failures_; }

    [[nodiscard]] std::uint32_t out_of_range() const noexcept { return out_of_range_; }

    [[nodiscard]] std::uint32_t reapplied() const noexcept { return reapplied_; }

private:
    void apply_(std::uint8_t player, std::uint16_t word) noexcept;

    svc::IRumbleOut& out_;
    RumblePadsCell& pads_;
    xthread::DiagLog& diag_;
    std::array<std::uint16_t, svc::kMaxPlayers> last_{};
    svc::RumblePadSet pads_seen_{};
    std::uint32_t applied_ = 0;
    std::uint32_t repeats_ = 0;
    std::uint32_t no_device_ = 0;
    std::uint32_t failures_ = 0;
    std::uint32_t out_of_range_ = 0;
    std::uint32_t reapplied_ = 0;
};

}
