// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/core_session.h"
#include "proto/types.h"

namespace mister::app {

struct ConfStrText {
    std::uint16_t len = 0;

    proto::BindGeneration gen{};

    bool truncated = false;
    char text[proto::CoreSession::kConfStrCap] = {};
};
static_assert(sizeof(ConfStrText::text) == proto::CoreSession::kConfStrCap,
              "the cell and the acquisition bound are the same number, joined "
              "here so they cannot drift");
static_assert(std::is_trivially_copyable_v<ConfStrText>);

using ConfStrCell = xthread::Telemetry<ConfStrText>;

}  // namespace mister::app
