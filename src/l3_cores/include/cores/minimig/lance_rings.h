// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "cores/minimig/a2065_mac_munge.h"
#include "cores/minimig/a2065_window.h"
#include "cores/minimig/lance_registers.h"
#include "infra/byte_count.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "svc/nic_frame.h"
#include "svc/nic_port.h"

namespace mister::cores::minimig {

class LanceRings {
    TASTY_SEAT_RESIDENT(Nic);

public:
    static constexpr infra::ByteCount kMinFrame{60};

    static constexpr infra::ByteCount kMinReceive{20};

    LanceRings(
        LanceRegisters& regs,
        A2065Window& ram,
        const A2065MacMunge& munge,
        svc::INicPort& port
    ) noexcept
        : regs_(regs),
          ram_(ram),
          munge_(munge),
          port_(port) {}

    LanceRings(const LanceRings&) = delete;
    LanceRings& operator=(const LanceRings&) = delete;

    bool transmit() noexcept;

    void receive(std::span<const std::byte> frame) noexcept;

    [[nodiscard]] bool rx_has_space() noexcept;

    void reset_loopback_count() noexcept { loopback_.set(infra::Tally{}); }

    [[nodiscard]] infra::Tally sent() const noexcept { return sent_; }

    [[nodiscard]] infra::Tally delivered() const noexcept { return delivered_; }

    [[nodiscard]] infra::Tally dropped_rx_off() const noexcept { return rx_off_; }

    [[nodiscard]] infra::Tally dropped_ring_full() const noexcept { return ring_full_; }

    [[nodiscard]] infra::Tally loopback_frames() const noexcept { return loopback_; }

private:
    [[nodiscard]] bool find_start_() noexcept;

    [[nodiscard]] bool starts_frame_(const LanceRing& ring) const noexcept;

    [[nodiscard]] bool accepted_(std::span<const std::byte> frame) const noexcept;

    LanceRegisters& regs_;
    A2065Window& ram_;
    const A2065MacMunge& munge_;
    svc::INicPort& port_;
    svc::NicFrame tx_{};
    svc::NicFrame rx_{};
    infra::Tally sent_{};
    infra::Tally delivered_{};
    infra::Tally rx_off_{};
    infra::Tally ring_full_{};
    infra::Tally loopback_{};
};

}
