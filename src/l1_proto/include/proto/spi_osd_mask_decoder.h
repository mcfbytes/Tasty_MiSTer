// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/link_event.h"
#include "proto/link_port.h"
#include "proto/link_router.h"
#include "proto/osd_focus.h"
#include "proto/types.h"

namespace mister::proto {

class SpiOsdMaskDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Port = LinkPort<LinkEvent::OsdMask>;

    static constexpr unsigned kPollTicks = 50;

    SpiOsdMaskDecoder(hal::ISpiTransport& link, ILinkRouter& router,
                      const IOsdFocus& focus) noexcept
        : link_(&link), focus_(&focus), out_(router) {}

    void service() noexcept;
    [[nodiscard]] bool active() const noexcept { return focus_->osd_focused(); }

    void forget(bool front_end) noexcept {
        seen_ = false;
        front_end_ = front_end;
    }

    [[nodiscard]] OsdMask mask() const noexcept { return mask_; }
    [[nodiscard]] std::uint32_t publishes() const noexcept { return publishes_; }
    [[nodiscard]] std::uint32_t errors() const noexcept { return errors_; }

private:
    hal::ISpiTransport* link_;
    const IOsdFocus* focus_;
    Port out_;
    OsdMask mask_{};
    bool seen_ = false;
    bool front_end_ = false;
    unsigned div_ = 0;
    std::uint32_t publishes_ = 0;
    std::uint32_t errors_ = 0;
};

}  // namespace mister::proto
