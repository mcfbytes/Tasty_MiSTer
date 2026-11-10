// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "app/picture_size.h"
#include "app/row_bytes.h"

namespace mister::app {

inline constexpr std::uint32_t kFramebufferPageBytes = 4096;

inline constexpr std::uint32_t kFramebufferSlotAlign = 4096;

struct FbView {

    enum class FbFormat : std::uint8_t { Rgb565, Argb8888, Argb1555, Pal8 };

    std::uint32_t offset = 0;

    PictureSize size{};
    RowBytes stride{};
    FbFormat format = FbFormat::Rgb565;

    bool red_blue_swap = false;

    [[nodiscard]] static constexpr FbView of(
        PictureSize size,
        RowBytes stride,
        FbFormat format,
        bool swap
    ) noexcept {
        return FbView{kFramebufferPageBytes, size, stride, format, swap};
    }

    [[nodiscard]] std::string mode_line() const {
        std::string line;
        switch (format) {
            case FbFormat::Argb8888:
                line = "8888";
                break;
            case FbFormat::Argb1555:
                line = "1555";
                break;
            case FbFormat::Rgb565:
                line = "565";
                break;
            case FbFormat::Pal8:
                line = "8";
                break;
        }
        line += red_blue_swap ? " 1 " : " 0 ";
        line += size.pixels().text().view();
        line += ' ';
        line += size.lines().text().view();
        line += ' ';
        line += stride.text().view();
        line += '\n';
        return line;
    }
};

}
