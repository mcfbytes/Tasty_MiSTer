// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/link.h"

#include "infra/prelude.h"
#include "proto/link/poll_frame_read.h"
#include "proto/link_sel.h"
#include "proto/user_io/poll_frame.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::proto::link {

Ex<hal::LinkWord> Link::command(LinkSel sel, hal::LinkWord op) {
    TASTY_SEAT_BODY(Link);
    return frame_.open(sel, op);
}

Ex<hal::LinkWord> Link::command(UioOp op) {
    TASTY_SEAT_BODY(Link);
    return frame_.open(LinkSel::Io, proto::word(op));
}

Ex<void> Link::word(hal::LinkWord w) {
    TASTY_SEAT_BODY(Link);
    return frame_.word(w);
}

Ex<hal::LinkWord> Link::read() {
    TASTY_SEAT_BODY(Link);

    return frame_.beat(hal::LinkWord{});
}

Ex<hal::LinkHalves> Link::read_dword() {
    TASTY_SEAT_BODY(Link);

    auto lo = read();
    if (!lo)
        return std::unexpected(lo.error());
    auto hi = read();
    if (!hi)
        return std::unexpected(hi.error());
    return hal::LinkHalves{.low = *lo, .high = *hi};
}

Ex<hal::LinkWord> Link::exchange(hal::LinkWord w) {
    TASTY_SEAT_BODY(Link);
    return frame_.beat(w);
}

bool Link::receive_(
    const user_io::PollFrameShape::Row& row,
    ReplyCount receives,
    PollFrameRead& out
) noexcept {
    for (ReplyCount done{}; done < receives; done += ReplyCount{1}) {
        const auto reply = exchange(row.exchanged);
        if (!reply)
            return false;
        out.frame.keep(user_io::kept_part(row.kind, *reply));
    }
    return true;
}

PollFrameRead Link::read_poll_frame(
    const user_io::PollFrameShape& shape,
    std::optional<ReplyWord> last_seen
) noexcept {
    TASTY_SEAT_BODY(Link);
    const user_io::PollFrameShape::Row& row = shape.row();
    PollFrameRead out{.charged = shape.max_words()};
    const auto opened = command(row.select, row.opcode);
    if (!opened)
        return out;
    if (row.keeps_command_reply)
        out.frame.keep(*opened);
    if (row.address) {
        const hal::LinkHalves at = row.address->halves();
        if (!word(at.low) || !word(at.high))
            return out;
    }
    if (!receive_(row, row.leading, out))
        return out;
    if (row.head_lengths
        && !receive_(row, row.head_lengths->after(out.frame.reply(row.head_lengths->head)), out))
        return out;
    const bool taken = shape.guard_holds(out.frame, last_seen);
    if (taken && !receive_(row, row.guarded, out))
        return out;
    close();
    out.outcome = taken ? PollFrameRead::Outcome::Taken : PollFrameRead::Outcome::Held;
    if (!taken)
        out.charged = shape.held_words();
    return out;
}

void Link::close() noexcept {
    frame_.abandon();
}

bool Link::open() const noexcept {
    return frame_.is_open();
}

}
