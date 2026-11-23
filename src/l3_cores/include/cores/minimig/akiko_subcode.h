// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "cores/minimig/cd_disc.h"
#include "proto/types.h"
#include "svc/disc/track.h"

namespace mister::cores::minimig::akiko {

using Subcode = std::array<std::byte, svc::disc::kSubcodeBytes>;

[[nodiscard]] Subcode subcode_channels(std::span<const Track> rows, proto::Lba lba) noexcept;

}
