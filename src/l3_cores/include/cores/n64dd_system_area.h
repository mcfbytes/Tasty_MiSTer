// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <string_view>

#include "cores/n64dd_disk_type.h"
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
    DiskType disk_type{};
    bool cart_expansion = false;
    Region region = Region::Unknown;

    [[nodiscard]] bool development() const noexcept { return region == Region::Development; }
};

[[nodiscard]] std::string_view root_ipl_name(Region region) noexcept;

[[nodiscard]] Ex<SystemArea> read_system_area(svc::IFile& file);

}
