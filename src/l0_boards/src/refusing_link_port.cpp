// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/refusing_link_port.h"

namespace mister::boards {

bool RefusingLinkPort::ready() const {
    TASTY_SEAT_BODY(RefusingLinkPort);
    return false;
}

hal::SpiSample RefusingLinkPort::sample() const noexcept {
    TASTY_SEAT_BODY(RefusingLinkPort);
    return {};
}

Ex<hal::CoreIdentity> RefusingLinkPort::identify() {
    TASTY_SEAT_BODY(RefusingLinkPort);
    return unimplemented(ERR_SITE());
}

hal::CoreCapabilities RefusingLinkPort::latch_capabilities() {
    TASTY_SEAT_BODY(RefusingLinkPort);
    return {};
}

void RefusingLinkPort::set_core_reset(bool) { TASTY_SEAT_BODY(RefusingLinkPort); }

void RefusingLinkPort::select(hal::ChipSelect) { TASTY_SEAT_BODY(RefusingLinkPort); }

void RefusingLinkPort::deselect() { TASTY_SEAT_BODY(RefusingLinkPort); }

Ex<hal::SpiWord> RefusingLinkPort::transfer(hal::SpiWord) {
    TASTY_SEAT_BODY(RefusingLinkPort);
    return unimplemented(ERR_SITE());
}

Ex<void> RefusingLinkPort::block_write(std::span<const std::uint8_t>) {
    TASTY_SEAT_BODY(RefusingLinkPort);
    return unimplemented(ERR_SITE());
}

Ex<void> RefusingLinkPort::block_read(std::span<std::uint8_t>) {
    TASTY_SEAT_BODY(RefusingLinkPort);
    return unimplemented(ERR_SITE());
}

Ex<void> RefusingLinkPort::post(hal::SpiWord) {
    TASTY_SEAT_BODY(RefusingLinkPort);
    return unimplemented(ERR_SITE());
}

Ex<bool> RefusingLinkPort::posted_done() {
    TASTY_SEAT_BODY(RefusingLinkPort);
    return unimplemented(ERR_SITE());
}

}  // namespace mister::boards
