// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "app/types.h"
#include "hal/types.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::app {

struct FileIntern {

    struct Piece {
        proto::FileSize whole{};
        proto::FileSize offset{};
    };

    std::vector<std::uint8_t> bytes{};
    std::string_view ext{};
    std::string_view path{};
    hal::FabricAddr load_addr{};
    svc::Crc32 crc{};
    Piece piece{};
    proto::FileId save{};
    ContentEdge edge = ContentEdge::Content;
};

}
