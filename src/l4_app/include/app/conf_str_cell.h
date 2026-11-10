// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>
#include <type_traits>

#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/core_session.h"
#include "proto/types.h"

namespace mister::app {

struct ConfStrText {
    TASTY_SEAT_EXEMPT(const_shared);

    FixedStr<proto::CoreSession::kConfStrCap + 1, StrFit::Clip> text{};

    proto::BindGeneration gen{};

    bool truncated = false;

    [[nodiscard]] std::string_view view() const noexcept { return text.view(); }
};

static_assert(
    decltype(ConfStrText::text)::kCapacity == proto::CoreSession::kConfStrCap,
    "the cell and the acquisition bound are the same number, joined "
    "here so they cannot drift"
);
static_assert(std::is_trivially_copyable_v<ConfStrText>);

using ConfStrCell = xthread::Telemetry<ConfStrText, SeatTag::Main>;

}
