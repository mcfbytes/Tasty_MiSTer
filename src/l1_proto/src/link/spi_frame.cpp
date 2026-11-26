// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/spi_frame.h"

#include <cstddef>

#include "infra/prelude.h"
#include "proto/link/link_sel_carriers.h"

namespace mister::proto::link {

Ex<void> SpiFrame::begin(LinkSel sel, hal::LinkWord op) {
    TASTY_SEAT_BODY(SpiFrame);
    return open(sel, op).transform([](hal::LinkWord) noexcept {});
}

Ex<hal::LinkWord> SpiFrame::open(LinkSel sel, hal::LinkWord op) {
    TASTY_SEAT_BODY(SpiFrame);
    return open_raw_(chip_select(sel), op);
}

Ex<hal::LinkWord> SpiFrame::open_raw_(hal::ChipSelect cs, hal::LinkWord op) {
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
    reply_ = *r;
    return *r;
}

Ex<void> SpiFrame::word(hal::LinkWord w) {
    TASTY_SEAT_BODY(SpiFrame);
    return beat(w).transform([](hal::LinkWord) noexcept {});
}

Ex<void> SpiFrame::end() {
    TASTY_SEAT_BODY(SpiFrame);
    if (!open_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    close_();
    return {};
}

std::optional<hal::LinkWord> SpiFrame::opcode_reply() const noexcept {
    TASTY_SEAT_BODY(SpiFrame);
    return reply_;
}

Ex<hal::LinkWord> SpiFrame::beat(hal::LinkWord w) {
    TASTY_SEAT_BODY(SpiFrame);
    if (!open_)
        return std::unexpected(w.refusal(Errc::negotiation, ERR_SITE()));
    auto r = link_.transfer(w);
    if (!r) {
        close_();
        return std::unexpected(r.error());
    }
    return *r;
}

void SpiFrame::close_() noexcept {
    if (!open_)
        return;
    open_ = false;
    link_.deselect();
}

}
