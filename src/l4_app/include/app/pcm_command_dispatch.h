// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "app/pcm_command.h"
#include "app/pcm_ring_feeder.h"
#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister::app {

template <class S>
concept PcmCommandSink = requires(S& s, const PcmCommand& m) {
    s.on(std::declval<const PcmCommand::Play&>());
    s.on(std::declval<const PcmCommand::Stop&>());
    s.on(std::declval<const PcmCommand::Resume&>());
    s.on(std::declval<const PcmCommand::AttachRing&>());
    s.on(std::declval<const PcmCommand::AttachSource&>());
    s.misrouted(m);
};

using PcmCommandRoutes = infra::AllRouted;

struct DrainSink {
    TASTY_SEAT_EXEMPT(component);
    PcmRingFeeder& feeder;
    std::uint32_t discarded = 0;

    void on(const PcmCommand::Play&) noexcept { ++discarded; }

    void on(const PcmCommand::Stop&) noexcept { ++discarded; }

    void on(const PcmCommand::Resume&) noexcept { ++discarded; }

    void on(const PcmCommand::AttachRing& a) noexcept { feeder.on(a); }

    void on(const PcmCommand::AttachSource& a) noexcept { feeder.on(a); }

    void misrouted(const PcmCommand& c) noexcept { feeder.misrouted(c); }
};

}
