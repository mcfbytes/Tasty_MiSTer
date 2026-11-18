// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/reg_access.h"

namespace mister::hal {

struct OsdBlock {
    enum class Reg : std::uint32_t {
        Image = 0x0000,
        Ctrl = 0x1000,
        Id = 0x1004,
    };
    static constexpr std::size_t kSize = 0x2000;

    static constexpr RegAccess kAccess = RegAccess::ReadWrite;
    static constexpr std::uint32_t kId = 0x3144534Fu;

    enum Layout : std::uint32_t { kWordBytes = 4 };

    static constexpr std::uint32_t kImageRows = 16;
    static constexpr std::uint32_t kRowBytes = 256;
    static constexpr std::uint32_t kRowWords = kRowBytes / kWordBytes;

    static constexpr std::uint32_t kGeometry = (kImageRows * 8u) << 16 | kRowBytes;

    static constexpr std::uint32_t kCtrlLwSel = 1u << 0;
    static constexpr std::uint32_t kCtrlHr = 1u << 1;

    static constexpr Reg image(std::size_t row, std::uint32_t w) noexcept {
        return static_cast<Reg>(row * kRowBytes + kWordBytes * w);
    }
};

static_assert(
    static_cast<std::uint32_t>(OsdBlock::image(OsdBlock::kImageRows - 1, OsdBlock::kRowWords - 1))
        == static_cast<std::uint32_t>(OsdBlock::Reg::Ctrl) - OsdBlock::kWordBytes,
    "the image is words 0x000-0xFFF; CTRL follows it"
);
static_assert(static_cast<std::size_t>(OsdBlock::Reg::Id) < OsdBlock::kSize);
static_assert(OsdBlock::kGeometry == 0x00800100u, "256 x 128, as the directory declares it");

}
