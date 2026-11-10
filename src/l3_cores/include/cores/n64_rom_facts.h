// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "cores/n64_detect_flags.h"
#include "cores/n64_rom.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores::n64 {

struct RomFacts {
    ByteOrder order = ByteOrder::Unknown;
    CartId cart_id{};
    InternalName internal_name{};
    FixedStr<33, StrFit::Reject> header_md5{};
    FixedStr<33, StrFit::Reject> file_md5{};
    svc::Crc32 crc32{};
    proto::FileSize bytes{};

    std::array<std::uint8_t, kControllerCount> controllers{};

    std::array<std::uint64_t, 2> bootcode{};
    DetectFlags detected{};
    bool homebrew = false;
};

}
