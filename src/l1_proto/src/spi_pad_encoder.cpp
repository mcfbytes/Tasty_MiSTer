// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_pad_encoder.h"

#include "hal/selected.h"

namespace mister::proto {

namespace {

[[nodiscard]] Ex<void> put(hal::ISpiTransport& link, std::uint16_t w) {
    auto r = link.transfer(hal::SpiWord{w});
    if (!r)
        return std::unexpected(r.error());
    return {};
}

}

Ex<void> SpiPadEncoder::digital(std::uint16_t opcode, JoyMask map) {
    TASTY_SEAT_BODY(SpiPadEncoder);
    if (!link_)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), opcode});
    hal::ISpiTransport& link = *link_;
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto e = put(link, opcode); !e)
        return e;
    if (auto e = put(link, static_cast<std::uint16_t>(map.v & 0xFFFFu)); !e)
        return e;

    return put(link, static_cast<std::uint16_t>(map.v >> 16));
}

Ex<void> SpiPadEncoder::stick(
    std::uint16_t opcode,
    std::uint8_t index,
    std::uint8_t x,
    std::uint8_t y
) {
    TASTY_SEAT_BODY(SpiPadEncoder);
    if (!link_)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), opcode});
    hal::ISpiTransport& link = *link_;
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto e = put(link, opcode); !e)
        return e;
    if (auto e = put(link, index); !e)
        return e;

    if (port_.wire().analog_word) {

        const auto w = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(y) << 8)
            | static_cast<std::uint16_t>(x)
        );
        return put(link, w);
    }

    if (auto e = put(link, x); !e)
        return e;
    return put(link, y);
}

std::uint32_t SpiPadEncoder::link_words(std::uint16_t, bool) const noexcept {
    TASTY_SEAT_BODY(SpiPadEncoder);
    return JoystickPort::kMaxDigitalPushWords;
}

}
