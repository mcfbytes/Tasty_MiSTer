// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_frame.h"

#include <cstddef>

#include "proto/link_sel_carriers.h"

namespace mister::proto {

Ex<void> SpiFrame::begin(LinkSel sel, hal::SpiWord op) {
    TASTY_SEAT_BODY(SpiFrame);
    return open(sel, op).transform([](std::uint16_t) noexcept {});
}

Ex<std::uint16_t> SpiFrame::open(LinkSel sel, hal::SpiWord op) {
    TASTY_SEAT_BODY(SpiFrame);
    return open_raw_(chip_select(sel), op);
}

Ex<std::uint16_t> SpiFrame::open_raw_(hal::ChipSelect cs, hal::SpiWord op) {
    TASTY_SEAT_BODY(SpiFrame);
    close_();
    reply_.reset();
    if (cs == hal::ChipSelect::None) {
        return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), static_cast<std::uint32_t>(cs)});
    }
    link_.select(cs);
    open_ = true;
    auto r = link_.transfer(op);
    if (!r) {
        close_();
        return std::unexpected(r.error());
    }
    reply_ = r->v;
    return r->v;
}

Ex<void> SpiFrame::word(std::uint16_t w) {
    TASTY_SEAT_BODY(SpiFrame);
    return beat(w).transform([](std::uint16_t) noexcept {});
}

Ex<void> SpiFrame::end() {
    TASTY_SEAT_BODY(SpiFrame);
    if (!open_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    close_();
    return {};
}

std::optional<std::uint16_t> SpiFrame::opcode_reply() const noexcept {
    TASTY_SEAT_BODY(SpiFrame);
    return reply_;
}

Ex<std::uint16_t> SpiFrame::beat(std::uint16_t w) {
    TASTY_SEAT_BODY(SpiFrame);
    if (!open_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), w});
    auto r = link_.transfer(hal::SpiWord{w});
    if (!r) {
        close_();
        return std::unexpected(r.error());
    }
    return r->v;
}

void SpiFrame::close_() noexcept {
    if (!open_)
        return;
    open_ = false;
    link_.deselect();
}

}
