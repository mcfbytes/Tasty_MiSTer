// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"

namespace mister::app {

inline constexpr proto::OsdRow kInfoOriginY{10};

inline constexpr proto::OsdRow kInfoOriginYDirectVideo{30};

[[nodiscard]] constexpr proto::OsdRow info_origin_y(bool direct_video, bool vga_fb) noexcept {
    return (direct_video && vga_fb) ? kInfoOriginYDirectVideo : kInfoOriginY;
}

}
