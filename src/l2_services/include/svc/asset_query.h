// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "svc/types.h"

namespace mister::svc {

struct AssetQuery {
    std::string_view rom_path;
    Crc32 crc{};
    std::string_view ext;
    std::string_view core_dir;

    std::string_view cd_asset_dir;

    bool require_zip = false;
};

}
