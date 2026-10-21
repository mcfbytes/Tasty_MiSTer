// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bitset>
#include <cstdint>

#include "cores/n64dd_geometry.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "svc/file.h"

namespace mister::cores::n64dd {

enum class Region : std::uint8_t { Unknown, Japan, Usa, Development };

struct SystemArea {
    std::array<std::uint8_t, kSystemSectorBytes> sys{};
    std::bitset<kLbaCount> bad_lba{};
    FixedStr<5, StrFit::Reject> disk_id{};
    std::uint8_t disk_type = 0;
    bool development = false;
    bool cart_expansion = false;
    Region region = Region::Unknown;
};

[[nodiscard]] Ex<SystemArea> read_system_area(svc::IFile& file);

}
