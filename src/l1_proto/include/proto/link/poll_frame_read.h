// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "proto/link/link.h"
#include "proto/link_sel.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::proto::link {

struct PollFrameRead {

    enum class Outcome : std::uint8_t { Faulted, Held, Taken };

    Outcome outcome = Outcome::Faulted;
    FioWordCount charged{};
    user_io::PollFrame frame{};
};

[[nodiscard]] inline PollFrameRead read_poll_frame(
    Link& link,
    const user_io::PollFrameShape& shape,
    std::optional<ReplyWord> last_seen
) noexcept {
    const user_io::PollFrameShape::Row& row = shape.row();
    PollFrameRead out{.charged = shape.max_words()};
    const auto receive = [&link, &row, &out](ReplyCount receives) noexcept {
        for (ReplyCount done{}; done < receives; done += ReplyCount{1}) {
            const auto reply = link.read();
            if (!reply)
                return false;
            out.frame.keep(user_io::kept_part(row.kind, *reply));
        }
        return true;
    };
    const auto command = link.command(LinkSel::Io, row.opcode);
    if (!command)
        return out;
    if (row.keeps_command_reply)
        out.frame.keep(*command);
    if (!receive(row.leading))
        return out;
    const bool taken = shape.guard_holds(out.frame, last_seen);
    if (taken && !receive(row.guarded))
        return out;
    link.close();
    out.outcome = taken ? PollFrameRead::Outcome::Taken : PollFrameRead::Outcome::Held;
    if (!taken)
        out.charged = shape.held_words();
    return out;
}

}
