// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "app/path_text.h"
#include "app/types.h"
#include "hal/types.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::app {

struct InternedFile {
    TASTY_SEAT_MEDIATOR(Any, Any);

    static constexpr std::uint16_t kRecycleSlots = 15;
    static constexpr std::uint16_t kNone = 0;

    static constexpr std::uint16_t kMaxLiveFiles = 14;

    static constexpr std::uint16_t kHeldOps = 1;

    static constexpr std::size_t kPieceBytes = 1024u * 1024u;

    std::span<const std::uint8_t> bytes{};
    FixedStr<8, StrFit::Reject> ext{};
    PathText path{};
    hal::FabricAddr load_addr{};
    proto::FileSize size_bytes{};
    std::uint64_t offset = 0;
    svc::Crc32 crc{};
    proto::FileId save{};
    std::uint32_t aux = 0;
    ContentEdge edge = ContentEdge::Content;

    bool remounts_saves = false;
};

static_assert(std::is_trivially_copyable_v<InternedFile>, "a pooled slot is recycled in place");

}
