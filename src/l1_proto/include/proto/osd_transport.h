// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"
#include "hal/selected.h"
#include "hal/spi_transport.h"
#include "proto/osd_surface.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

enum class OsdTarget : std::uint8_t {
    Hdmi = 1,
    Vga = 2,
    All = 3,
};

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

    [[nodiscard]] Ex<void> flush_row(hal::ISpiTransport& link, OsdRow r,
                                     std::span<const std::uint8_t> bytes);

    Ex<void> set_visible(hal::ISpiTransport& link, bool visible);

    Ex<void> enable(hal::ISpiTransport& link, OsdMode mode);

    Ex<void> enable_info(hal::ISpiTransport& link, std::uint16_t x, std::uint16_t y,
                         std::uint16_t width, std::uint16_t height);

    Ex<void> set_rotation(hal::ISpiTransport& link, OsdRotation rotate);

    Ex<void> menu_ctl(hal::ISpiTransport& link, bool enable_menu);

    [[nodiscard]] Ex<void> set_target(OsdTarget t) noexcept {
        target_ = t;
        return {};
    }
    OsdTarget target() const noexcept { return target_; }

    static constexpr hal::SpiOutput select_mask(OsdTarget t) noexcept {
        const auto bits = static_cast<std::uint8_t>(t);
        std::uint32_t m = kOsdEn | kIoEn | kFpgaEn;
        if ((bits & static_cast<std::uint8_t>(OsdTarget::Hdmi)) != 0u) m &= ~kFpgaEn;
        if ((bits & static_cast<std::uint8_t>(OsdTarget::Vga)) != 0u) m &= ~kIoEn;
        return hal::SpiOutput{m};
    }

    static constexpr hal::SpiOutput kDeselectMask{kOsdEn | kIoEn | kFpgaEn};

    std::uint32_t unrouted_selects() const noexcept { return unrouted_selects_; }

private:
    hal::Selected begin(hal::ISpiTransport& link);
    Ex<void> command(hal::ISpiTransport& link, std::uint16_t cmd);

    OsdTarget target_ = OsdTarget::All;
    std::uint32_t unrouted_selects_ = 0;
};

}  // namespace mister::proto
