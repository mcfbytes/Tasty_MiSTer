// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <variant>

#include "cores/mailbox_types.h"
#include "cores/minimig/amiga_date.h"
#include "cores/minimig/share_cursor.h"
#include "cores/minimig/share_key.h"
#include "cores/minimig/share_wire.h"
#include "infra/byte_offset.h"
#include "proto/types.h"

namespace mister::cores::minimig {

struct ShareReply {

    struct Examined {
        ShareCursor cursor{};
        ShareEntry entry = ShareEntry::None;
        proto::FileSize size{};
        AmigaDate date{};
        std::string name{};
    };

    struct Volume {
        proto::FileSize total{};
        proto::FileSize used{};
    };

    struct LockAnswer {
        ShareLockKey key{};
    };

    struct FileAnswer {
        ShareFileKey key{};
    };

    struct Count {
        MailboxLen bytes{};
    };

    struct Position {
        proto::FileOffset at{};
    };

    struct Bare {};

    using Body = std::variant<Bare, LockAnswer, FileAnswer, Count, Position, Examined, Volume>;
    static_assert(std::variant_size_v<Body> == 7, "a new reply body needs its encode arm");

    ShareAction action = ShareAction::Nil;
    ShareError error = ShareError::None;
    Body body = Bare{};
    std::optional<infra::ByteOffset> terminator{};
};

}
