// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "cores/minimig/share_cursor.h"
#include "cores/minimig/share_key.h"
#include "cores/minimig/share_seek_to.h"
#include "cores/minimig/share_wire.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"

namespace mister::cores::minimig {

struct ShareRequest {
    ShareAction action = ShareAction::Nil;
    ShareLockKey lock{};
    ShareLockKey other{};
    ShareFileKey file{};
    ShareCursor cursor{};
    ShareSeekTo seek{};
    ShareSeek seek_from = ShareSeek::Beginning;
    infra::ByteCount length{};
    std::string name{};
    std::string new_name{};

    std::optional<infra::ByteOffset> terminator{};
};

}
