// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link.h"

namespace mister::proto {

Ex<std::uint16_t> Link::command(hal::ChipSelect cs, std::uint16_t op) {
    TASTY_SEAT_BODY(Link);
    return frame_.open(cs, op);
}

Ex<std::uint16_t> Link::command(FrameSel sel, std::uint16_t op) {
    TASTY_SEAT_BODY(Link);
    return frame_.open(sel, op);
}

Ex<std::uint16_t> Link::command(LinkRoute route, std::uint16_t op) {
    TASTY_SEAT_BODY(Link);
    return frame_.open(route, op);
}

Ex<std::uint16_t> Link::command(UioOp op) {
    TASTY_SEAT_BODY(Link);
    return frame_.open(LinkRoute::Uio, proto::word(op).v);
}

Ex<void> Link::word(std::uint16_t w) {
    TASTY_SEAT_BODY(Link);
    return frame_.word(w);
}

Ex<std::uint16_t> Link::read() {
    TASTY_SEAT_BODY(Link);

    return frame_.beat(0);
}

Ex<std::uint32_t> Link::read_dword() {
    TASTY_SEAT_BODY(Link);

    auto lo = read();
    if (!lo)
        return std::unexpected(lo.error());
    auto hi = read();
    if (!hi)
        return std::unexpected(hi.error());
    return static_cast<std::uint32_t>(*lo) | (static_cast<std::uint32_t>(*hi) << 16);
}

void Link::close() noexcept {
    frame_.abandon();
}

bool Link::open() const noexcept {
    return frame_.is_open();
}

}
