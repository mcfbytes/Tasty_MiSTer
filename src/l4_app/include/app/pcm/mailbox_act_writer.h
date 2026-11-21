// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/xthread/counter.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/mailbox_act.h"
#include "proto/link/spi_fio_queue.h"
#include "proto/uio_opcodes.h"

namespace mister::app::pcm {

namespace detail {

class ActWriter final {
    TASTY_SEAT_RESIDENT(RT);

public:
    proto::link::SpiFioQueue& queue;
    std::span<const std::uint8_t> bytes;
    xthread::Counter& acts;
    xthread::Counter& refusals;
    xthread::Counter& none_acts;

    void on(const proto::MailboxAct::None&) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        none_acts.add();
    }

    void on(const proto::MailboxAct::Command& a) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        if (queue.append_command(proto::command_words(a.opcode, a.words)).has_value()) {
            acts.add();
        } else {
            refusals.add();
        }
    }

    void on(const proto::MailboxAct::Download& d) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        const std::size_t n = std::min<std::size_t>(d.len, bytes.size());
        const proto::link::SpiFioQueue::Bracket b[] = {{d.index, bytes.first(n)}};
        if (queue.append(b).has_value()) {
            acts.add();
        } else {
            refusals.add();
        }
    }

    void misrouted(const proto::MailboxAct&) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        refusals.add();
    }
};

}

}
