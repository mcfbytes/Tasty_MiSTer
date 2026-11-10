// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "svc/modeline.h"
#include "svc/pll_block.h"
#include "svc/types.h"

namespace mister::svc {

enum class ModeRequest : std::uint8_t {
    PresetIndex,
    Triple,
    Nine,
    Eleven,
    RawModeline,
};

namespace video_timing {

struct ParsedMode {
    Modeline mode{};
    ModeRequest how = ModeRequest::PresetIndex;

    PresetIndex preset_index{};

    std::optional<PllBlock> raw_pll{};
};

}

}
