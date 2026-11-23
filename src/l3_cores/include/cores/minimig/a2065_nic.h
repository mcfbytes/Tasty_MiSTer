// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>

#include "cores/minimig/a2065_command_ring.h"
#include "cores/minimig/a2065_mac_munge.h"
#include "cores/minimig/a2065_window.h"
#include "cores/minimig/lance_events.h"
#include "cores/minimig/lance_registers.h"
#include "cores/minimig/lance_rings.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "svc/nic_frame.h"
#include "svc/nic_grant.h"
#include "svc/nic_link_plan.h"
#include "svc/nic_personality.h"
#include "svc/nic_poll.h"
#include "svc/nic_port.h"

namespace mister::cores::minimig {

class A2065Nic final : public svc::INicPersonality, private ILanceEvents {
    TASTY_SEAT_RESIDENT(Nic);

public:
    static constexpr infra::Tally kRxBatch{64};

    explicit A2065Nic(const svc::NicGrant& grant) noexcept;
    A2065Nic(const A2065Nic&) = delete;
    A2065Nic& operator=(const A2065Nic&) = delete;

    [[nodiscard]] Ex<void> start() override;
    void reset() noexcept override;
    [[nodiscard]] svc::NicPoll poll() noexcept override;
    void stop() noexcept override;

    [[nodiscard]] const LanceRegisters& registers() const noexcept { return regs_; }

    [[nodiscard]] const LanceRings& rings() const noexcept { return rings_; }

    [[nodiscard]] const A2065MacMunge& munge() const noexcept { return munge_; }

    [[nodiscard]] bool offline() const noexcept { return offline_; }

private:
    void initialised(const svc::MacAddress& station) noexcept override;
    void transmit_demand() noexcept override;
    void interrupt() noexcept override;

    void push_shadow_() noexcept;

    void follow_promiscuous_() noexcept;
    [[nodiscard]] infra::Tally drain_rx_() noexcept;

    A2065Window window_;
    svc::INicPort& port_;
    std::optional<svc::NicLinkPlan> plan_;
    A2065CommandRing commands_;
    A2065MacMunge munge_{};
    LanceRegisters regs_;
    LanceRings rings_;
    svc::NicFrame frame_{};
    infra::Tally sent_this_pass_{};
    bool batching_ = false;
    bool prom_active_ = false;
    bool offline_ = false;
};

[[nodiscard]] std::unique_ptr<svc::INicPersonality> make_a2065_nic(const svc::NicGrant& grant);

}
