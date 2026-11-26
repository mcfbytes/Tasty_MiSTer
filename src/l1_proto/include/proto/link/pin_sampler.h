// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "hal/pin_levels.h"
#include "hal/pin_sample_source.h"
#include "proto/link_event.h"
#include "proto/link_port.h"
#include "proto/link_router.h"
#include "proto/session_live.h"

namespace mister::proto::link {

class PinSampler {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Port = LinkPort<LinkEvent::ReadyEdge, LinkEvent::ButtonLevel>;

    PinSampler(
        hal::IPinSampleSource& src,
        ILinkRouter& router,
        const ISessionLive& live,
        hal::PinLevelCell& levels
    ) noexcept
        : src_(src),
          live_(live),
          out_(router),
          levels_(levels) {}

    void service(bool hold_buttons = false) noexcept;

    [[nodiscard]] bool active() const noexcept { return live_.session_live(); }

    [[nodiscard]] const hal::PinLevelCell& levels() const noexcept { return levels_; }

    [[nodiscard]] const hal::PinSample& level() const noexcept { return last_; }

    [[nodiscard]] bool seen() const noexcept { return seen_; }

private:
    hal::IPinSampleSource& src_;
    const ISessionLive& live_;
    Port out_;
    hal::PinLevelCell& levels_;
    hal::PinSample last_{};
    bool seen_ = false;
};

}
