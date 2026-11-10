// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "cores/neogeo_file_index.h"

namespace mister::cores::neogeo {

struct RomsetFile {
    std::string name{};
    std::string path{};
    FileIndex index = FileIndex::Bios;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t expand = 0;
    bool swap = false;
};

}
