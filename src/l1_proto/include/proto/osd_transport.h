// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"
#include "hal/spi_transport.h"
#include "proto/info_window.h"
#include "proto/link_frame.h"
#include "proto/link_sel.h"
#include "proto/osd_surface.h"
#include "proto/osd_rotation.h"
#include "proto/osd_target.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

enum class OsdMode : std::uint8_t {
    Plain = 0x00,
    DisableKeyboard = 0x02,
    Message = 0x08,
};

class OsdTransport {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] Ex<void> flush_row(
        ILinkFrame& frame,
        OsdRow r,
        std::span<const std::uint8_t> bytes
    );

    [[nodiscard]] Ex<void> set_visible(ILinkFrame& frame, bool visible);

    [[nodiscard]] Ex<void> enable(ILinkFrame& frame, OsdMode mode);

    [[nodiscard]] Ex<void> enable_info(ILinkFrame& frame, InfoWindow window);

    [[nodiscard]] Ex<void> set_rotation(ILinkFrame& frame, OsdRotation rotate);

    [[nodiscard]] Ex<void> menu_ctl(ILinkFrame& frame, bool enable_menu);

    [[nodiscard]] Ex<void> set_target(OsdTarget t) noexcept {
        target_ = t;
        return {};
    }

    OsdTarget target() const noexcept { return target_; }

    std::uint32_t unrouted_selects() const noexcept { return unrouted_selects_; }

private:
    [[nodiscard]] LinkSel sel() const noexcept;
    [[nodiscard]] Ex<void> command(ILinkFrame& frame, std::uint16_t cmd);

    OsdTarget target_ = OsdTarget::All;
    std::uint32_t unrouted_selects_ = 0;
};

}
