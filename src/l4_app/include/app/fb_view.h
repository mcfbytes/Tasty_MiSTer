// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

struct FbView {
    enum class FbFormat : std::uint8_t { Rgb565, Argb8888 };
    std::uint32_t offset = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    FbFormat format = FbFormat::Rgb565;
};

}  // namespace mister::app
