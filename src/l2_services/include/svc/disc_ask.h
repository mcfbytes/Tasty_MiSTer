// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "proto/types.h"
#include "svc/sector_header.h"

namespace mister::svc {

enum class DiscForm : std::uint8_t { UserData, RawFrame, Subcode, FullFrame };

struct DiscAsk {
    DiscForm form = DiscForm::RawFrame;
    proto::Lba lba{};
    std::optional<SectorHeader> header{};
    friend constexpr bool operator==(DiscAsk, DiscAsk) = default;
};

}
