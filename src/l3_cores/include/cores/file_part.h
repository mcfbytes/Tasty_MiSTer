// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>

#include "cores/part_common.h"
#include "cores/types.h"
#include "svc/types.h"

namespace mister::cores::mra {

struct FilePart {
    PartCommon common;
    std::string name;
    std::string zips;
    svc::Crc32 crc{};
    std::optional<RomOffset> offset = RomOffset{};
    std::optional<RomLength> length{};
};

}
