// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"
#include "svc/sector_header.h"

namespace mister::svc {

enum class DiscForm : std::uint8_t { UserData, RawFrame, Subcode, FullFrame };

struct DiscAsk {
    DiscForm form = DiscForm::RawFrame;
    proto::Lba lba{};
    SectorHeader header{};
    friend constexpr bool operator==(DiscAsk, DiscAsk) = default;
};

}
