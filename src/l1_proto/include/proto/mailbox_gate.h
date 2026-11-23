// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "proto/mailbox_select_index.h"
#include "proto/types.h"

namespace mister::proto {

struct MailboxGate {
    MailboxSelectIndex on{};
    ReplyMask mask{};

    [[nodiscard]] constexpr bool holds(std::optional<ReplyWord> read) const noexcept {
        return read && read->intersects(mask);
    }

    friend constexpr bool operator==(const MailboxGate&, const MailboxGate&) = default;
};

}
