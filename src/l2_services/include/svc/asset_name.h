// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "svc/types.h"

namespace mister::svc::asset_name {

bool extension_matches(std::string_view filename, std::string_view packed_extensions);

std::optional<Crc32> parse_crc_name(std::string_view filename, std::string_view ext);

struct ZipSplit {
    bool zipped = false;
    std::string_view archive;
    std::string_view member;
};

ZipSplit zip_split(std::string_view path) noexcept;

}
