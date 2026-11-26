// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/refusing_board_link.h"

#include "infra/prelude.h"

namespace mister::boards {

bool RefusingBoardLink::ready() const {
    TASTY_SEAT_BODY(RefusingBoardLink);
    return false;
}

hal::PinSample RefusingBoardLink::sample() const noexcept {
    TASTY_SEAT_BODY(RefusingBoardLink);
    return {};
}

Ex<hal::CoreTypeByte> RefusingBoardLink::identify() {
    TASTY_SEAT_BODY(RefusingBoardLink);
    return unimplemented(ERR_SITE());
}

hal::CoreCapabilities RefusingBoardLink::latch_capabilities() {
    TASTY_SEAT_BODY(RefusingBoardLink);
    return {};
}

void RefusingBoardLink::set_core_reset(bool) {
    TASTY_SEAT_BODY(RefusingBoardLink);
}

void RefusingBoardLink::select(hal::ChipSelect) {
    TASTY_SEAT_BODY(RefusingBoardLink);
}

void RefusingBoardLink::deselect() {
    TASTY_SEAT_BODY(RefusingBoardLink);
}

Ex<hal::LinkWord> RefusingBoardLink::transfer(hal::LinkWord) {
    TASTY_SEAT_BODY(RefusingBoardLink);
    return unimplemented(ERR_SITE());
}

Ex<void> RefusingBoardLink::block_write(std::span<const std::uint8_t>) {
    TASTY_SEAT_BODY(RefusingBoardLink);
    return unimplemented(ERR_SITE());
}

Ex<void> RefusingBoardLink::block_read(std::span<std::uint8_t>) {
    TASTY_SEAT_BODY(RefusingBoardLink);
    return unimplemented(ERR_SITE());
}

Ex<void> RefusingBoardLink::post(hal::LinkWord) {
    TASTY_SEAT_BODY(RefusingBoardLink);
    return unimplemented(ERR_SITE());
}

Ex<bool> RefusingBoardLink::posted_done() {
    TASTY_SEAT_BODY(RefusingBoardLink);
    return unimplemented(ERR_SITE());
}

}
