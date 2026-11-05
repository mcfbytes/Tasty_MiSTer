// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"
#include "hal/spi_transport.h"
#include "proto/osd_surface.h"
#include "proto/link.h"
#include "proto/osd_target.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

enum class OsdMode : std::uint8_t {
    Plain = 0x00,
    DisableKeyboard = 0x02,
    Message = 0x08,
};

enum class OsdRotation : std::uint8_t { None = 0, Left = 1, Right = 3 };

class OsdTransport {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::uint32_t kFpgaEn = 1u << 18;
    static constexpr std::uint32_t kOsdEn = 1u << 19;
    static constexpr std::uint32_t kIoEn = 1u << 20;

    [[nodiscard]] Ex<void> flush_row(
        proto::Link& link,
        OsdRow r,
        std::span<const std::uint8_t> bytes
    );

    [[nodiscard]] Ex<void> set_visible(proto::Link& link, bool visible);

    [[nodiscard]] Ex<void> enable(proto::Link& link, OsdMode mode);

    [[nodiscard]] Ex<void> enable_info(
        proto::Link& link,
        std::uint16_t x,
        std::uint16_t y,
        std::uint16_t width,
        std::uint16_t height
    );

    [[nodiscard]] Ex<void> set_rotation(proto::Link& link, OsdRotation rotate);

    [[nodiscard]] Ex<void> menu_ctl(proto::Link& link, bool enable_menu);

    [[nodiscard]] Ex<void> set_target(OsdTarget t) noexcept {
        target_ = t;
        return {};
    }

    OsdTarget target() const noexcept { return target_; }

    static constexpr hal::SpiOutput select_mask(OsdTarget t) noexcept {
        std::uint32_t m = kOsdEn | kIoEn | kFpgaEn;
        if (targets(t, OsdTarget::Hdmi))
            m &= ~kFpgaEn;
        if (targets(t, OsdTarget::Vga))
            m &= ~kIoEn;
        return hal::SpiOutput{m};
    }

    static constexpr hal::SpiOutput kDeselectMask{kOsdEn | kIoEn | kFpgaEn};

    std::uint32_t unrouted_selects() const noexcept { return unrouted_selects_; }

private:
    [[nodiscard]] hal::ChipSelect chip_select() const noexcept;
    [[nodiscard]] Ex<void> command(proto::Link& link, std::uint16_t cmd);

    OsdTarget target_ = OsdTarget::All;
    std::uint32_t unrouted_selects_ = 0;
};

}
