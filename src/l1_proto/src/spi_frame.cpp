// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_frame.h"

#include <array>
#include <cstddef>

namespace mister::proto {

namespace {

constexpr hal::ChipSelect select_of(FrameSel sel) noexcept {
    switch (sel) {
        case FrameSel::Uio:
            return hal::ChipSelect::Io;
        case FrameSel::Fpga:
            return hal::ChipSelect::Fpga;
        case FrameSel::OsdHdmi:
            return hal::ChipSelect::OsdHdmi;
        case FrameSel::OsdVga:
            return hal::ChipSelect::OsdVga;
    }
    return hal::ChipSelect::None;
}

constexpr hal::ChipSelect select_of(LinkRoute route) noexcept {
    switch (route) {
        case LinkRoute::Uio:
            return hal::ChipSelect::Io;
        case LinkRoute::Fio:
            return hal::ChipSelect::Fpga;
        case LinkRoute::Osd:
            return hal::ChipSelect::Osd;
    }
    return hal::ChipSelect::None;
}

}

Ex<void> SpiFrame::begin(FrameSel sel, std::uint16_t op) {
    TASTY_SEAT_BODY(SpiFrame);
    return open(sel, op).transform([](std::uint16_t) noexcept {});
}

Ex<std::uint16_t> SpiFrame::open(FrameSel sel, std::uint16_t op) {
    TASTY_SEAT_BODY(SpiFrame);
    return open(select_of(sel), op);
}

Ex<void> SpiFrame::begin(LinkRoute route, std::uint16_t op) {
    TASTY_SEAT_BODY(SpiFrame);
    return open(route, op).transform([](std::uint16_t) noexcept {});
}

Ex<std::uint16_t> SpiFrame::open(LinkRoute route, std::uint16_t op) {
    TASTY_SEAT_BODY(SpiFrame);
    return open(select_of(route), op);
}

Ex<std::uint16_t> SpiFrame::open(hal::ChipSelect cs, std::uint16_t op) {
    TASTY_SEAT_BODY(SpiFrame);
    close_();
    reply_.reset();
    if (cs == hal::ChipSelect::None) {
        return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), static_cast<std::uint32_t>(cs)});
    }
    link_.select(cs);
    open_ = true;
    auto r = link_.transfer(hal::SpiWord{op});
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
