// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/link.h"

#include "infra/prelude.h"

namespace mister::proto::link {

Ex<hal::SpiWord> Link::command(LinkSel sel, hal::SpiWord op) {
    TASTY_SEAT_BODY(Link);
    return frame_.open(sel, op);
}

Ex<hal::SpiWord> Link::command(UioOp op) {
    TASTY_SEAT_BODY(Link);
    return frame_.open(LinkSel::Io, proto::word(op));
}

Ex<void> Link::word(hal::SpiWord w) {
    TASTY_SEAT_BODY(Link);
    return frame_.word(w);
}

Ex<hal::SpiWord> Link::read() {
    TASTY_SEAT_BODY(Link);

    return frame_.beat(hal::SpiWord{});
}

Ex<hal::SpiHalves> Link::read_dword() {
    TASTY_SEAT_BODY(Link);

    auto lo = read();
    if (!lo)
        return std::unexpected(lo.error());
    auto hi = read();
    if (!hi)
        return std::unexpected(hi.error());
    return hal::SpiHalves{.low = *lo, .high = *hi};
}

void Link::close() noexcept {
    frame_.abandon();
}

bool Link::open() const noexcept {
    return frame_.is_open();
}

}
