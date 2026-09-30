// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/fixed_str.h"
#include "svc/types.h"

namespace mister::app {

inline constexpr std::size_t kCaptureIdCap = 128;

struct CaptureHit {
    std::uint32_t token = 0;
    std::uint32_t unique_hash = 0;
    std::uint16_t code = 0;
    std::uint8_t device_ordinal = 0;
    bool mod = false;
    bool unique_filenames = false;
    bool id_clipped = false;
    svc::Vid vid{};
    svc::Pid pid{};
    FixedStr<kCaptureIdCap, StrFit::Clip> id{};
};

}  // namespace mister::app
