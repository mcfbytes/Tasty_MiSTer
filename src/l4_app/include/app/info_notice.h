// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "hal/ocram_info_arg.h"
#include "infra/prelude.h"
#include "infra/tally.h"
#include "infra/xthread/telemetry.h"
#include "proto/types.h"

namespace mister::app {

struct InfoNotice {
    hal::ocram::InfoArg info{};
    proto::BindGeneration bind_gen{};
    infra::Tally count{};
};

static_assert(std::is_trivially_copyable_v<InfoNotice>);
using InfoNoticeCell = xthread::Telemetry<InfoNotice, SeatTag::Io>;

}
