// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/row_bytes.h"

namespace mister::app {

inline constexpr std::uint32_t kFramebufferPageBytes = 4096;

inline constexpr std::uint32_t kFramebufferSlotAlign = 4096;

struct FbView {
    enum class FbFormat : std::uint8_t { Rgb565, Argb8888 };
    std::uint32_t offset = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    RowBytes stride{};
    FbFormat format = FbFormat::Rgb565;
};

}
