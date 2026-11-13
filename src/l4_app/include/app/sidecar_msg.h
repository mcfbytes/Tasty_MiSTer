// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "app/chunk_place.h"
#include "app/frame_stamp.h"
#include "app/picture_pixels.h"
#include "app/picture_row.h"
#include "app/rec_control.h"
#include "app/types.h"
#include "infra/inbox.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "svc/types.h"

namespace mister::app {

struct SidecarMsg {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t { Open, Row, Close, kCount };

    struct Head {
        ControlGeneration gen{};
    };

    struct Open {
        static constexpr Kind kKind = Kind::Open;
        RecPath path{};
    };

    struct Row {
        static constexpr Kind kKind = Kind::Row;
        PicturePixels width{};
        PictureRows height{};
        svc::Crc32 hash{};
        ChunkPlace place{};
        FrameStamp stamp{};
    };

    struct Close {
        static constexpr Kind kKind = Kind::Close;
    };

    using Alternatives = std::tuple<Open, Row, Close>;

    static constexpr std::size_t kStore = (sizeof(Open) + alignof(Row) - 1)
        / alignof(Row)
        * alignof(Row);

    Kind kind = Kind::Open;
    std::uint8_t pad_[5]{};
    Head head{};
    alignas(Row) std::array<std::byte, kStore> store{};
};

static_assert(
    infra::MessageSum<SidecarMsg>
    && infra::HasHead<SidecarMsg>
    && infra::alternatives_are_total<SidecarMsg>()
);

inline constexpr std::size_t kSidecarRingRows = 512;

using SidecarRing = xthread::Inbox<SidecarMsg, kSidecarRingRows>;

}
