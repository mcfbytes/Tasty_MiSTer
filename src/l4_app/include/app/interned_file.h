// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "app/path_text.h"
#include "app/types.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app {

struct InternedFile {
    TASTY_SEAT_MEDIATOR(Any, Any);

    static constexpr std::uint16_t kRecycleSlots = 15;
    static constexpr std::uint16_t kConfigSlots = 10;
    static constexpr std::uint16_t kConfigBase = kRecycleSlots + 1;
    static constexpr std::uint16_t kSlots = kConfigBase + kConfigSlots;
    static constexpr std::uint16_t kNone = 0;

    static constexpr std::uint16_t kMaxLiveFiles = 14;

    static constexpr std::uint16_t kHeldOps = 1;

    static constexpr std::size_t kPieceBytes = 1024u * 1024u;

    [[nodiscard]] static constexpr proto::FileId config_id(std::uint8_t slot) noexcept {
        return proto::FileId{static_cast<std::uint16_t>(kConfigBase + slot)};
    }

    std::span<const std::uint8_t> bytes{};
    FixedStr<8, StrFit::Reject> ext{};
    PathText path{};
    std::uint32_t load_addr = 0;
    std::uint64_t size_bytes = 0;
    std::uint64_t offset = 0;
    std::uint32_t crc = 0;
    proto::FileId save{};
    std::uint32_t aux = 0;
    ContentEdge edge = ContentEdge::Content;
};
static_assert(std::is_trivially_copyable_v<InternedFile>, "a pooled slot is recycled in place");

}  // namespace mister::app
