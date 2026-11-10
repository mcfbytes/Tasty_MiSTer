// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "app/rec_options.h"
#include "app/recording_name.h"
#include "app/types.h"
#include "infra/fixed_str.h"
#include "infra/loan_channel.h"
#include "infra/seat.h"

namespace mister::app {

enum class RecOp : std::uint8_t { None, Start, Stop, Arm, Disarm };

enum class RecMode : std::uint8_t { Hash, Avi };

enum class RecVerdict : std::uint8_t {
    None,
    Started,
    Stopped,
    NoWindow,
    NoLiveBuffer,
    Busy,
    WriterFailed,
    CoreSwitch,
    ReplayEnded,
    NoCodec,
    NoMemory,

    ScalerPortStuck,
    kCount,
};

struct RecRequest {
    TASTY_SEAT_MEDIATOR(Any, Any);

    ControlGeneration gen{};
    RecOp op = RecOp::None;
    RecMode mode = RecMode::Hash;
    RecPath path{};
    RecOptions opt{};
    RecVerdict verdict = RecVerdict::None;
};

static_assert(std::is_trivially_copyable_v<RecRequest>);
static_assert(std::is_nothrow_default_constructible_v<RecRequest>);

using RecControlChannel = xthread::LoanChannel<RecRequest, 2, SeatTag::Ui, SeatTag::Capture>;

}
