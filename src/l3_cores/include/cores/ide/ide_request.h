// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <cstdint>
#include <optional>

#include "proto/types.h"

namespace mister::cores::ide {

enum class IdeRequest : std::uint8_t { None = 0, Command = 4, Data = 5, Reset = 6 };

[[nodiscard]] constexpr std::optional<IdeRequest> ide_request_of(
    proto::ReplyWord head,
    proto::ReplyMask lane
) noexcept {
    if (!lane.any())
        return std::nullopt;
    const int shift = std::countr_zero(lane.v);
    const IdeRequest named = static_cast<IdeRequest>((head.v & lane.v) >> shift);
    switch (named) {
        case IdeRequest::None:
        case IdeRequest::Command:
        case IdeRequest::Data:
        case IdeRequest::Reset:
            return named;
    }
    return std::nullopt;
}

}
