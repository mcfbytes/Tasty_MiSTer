// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "hal/pin_levels.h"
#include "hal/spi_sample_source.h"
#include "proto/link_event.h"
#include "proto/link_port.h"
#include "proto/link_router.h"
#include "proto/session_live.h"

namespace mister::proto {

class SpiSampler {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Port = LinkPort<LinkEvent::ReadyEdge, LinkEvent::ButtonLevel>;

    SpiSampler(hal::ISpiSampleSource& src, ILinkRouter& router, const ISessionLive& live,
               hal::PinLevelCell& levels) noexcept
        : src_(&src), live_(&live), out_(router), levels_(&levels) {}

    void service(bool hold_buttons = false) noexcept;
    [[nodiscard]] bool active() const noexcept { return live_->session_live(); }

    [[nodiscard]] const hal::PinLevelCell& levels() const noexcept { return *levels_; }

    [[nodiscard]] const hal::SpiSample& level() const noexcept { return last_; }
    [[nodiscard]] bool seen() const noexcept { return seen_; }

private:
    hal::ISpiSampleSource* src_;
    const ISessionLive* live_;
    Port out_;
    hal::PinLevelCell* const levels_;
    hal::SpiSample last_{};
    bool seen_ = false;
};

}  // namespace mister::proto
