// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/link_event.h"
#include "proto/link_port.h"
#include "proto/link_router.h"

namespace mister::proto {

class SpiPs2Decoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Port = LinkPort<LinkEvent::Ps2Control, LinkEvent::Ps2ControlEnded>;

    SpiPs2Decoder(hal::ISpiTransport& link, ILinkRouter& router) noexcept
        : link_(&link), out_{router} {}

    void probe() noexcept;

    void service() noexcept;

    void forget() noexcept;
    [[nodiscard]] bool active() const noexcept { return true; }

    bool control_owned() const noexcept { return ps2_control_; }

    [[nodiscard]] std::uint32_t errors() const noexcept {
        return errors_.load(std::memory_order_relaxed);
    }

private:
    void count_error_() noexcept {
        errors_.store(errors_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }

    hal::ISpiTransport* link_;
    Port out_;
    bool ps2_control_ = false;
    static constexpr std::uint16_t kLedPollTicks = 100;
    std::uint16_t led_poll_div_ = 0;
    std::atomic<std::uint32_t> errors_{0};
};

}  // namespace mister::proto
