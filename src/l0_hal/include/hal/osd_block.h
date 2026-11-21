// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/reg_access.h"
#include "hal/reg_word.h"

namespace mister::hal {

struct OsdBlock {
    enum class Reg : std::uint32_t {
        Image = 0x0000,
        Status = 0x1800,
        Id = 0x1804,
    };
    static constexpr std::size_t kSize = 0x2000;

    static constexpr RegAccess kAccess = RegAccess::ReadWrite;
    static constexpr std::uint32_t kId = 0x3244534Fu;

    enum Layout : std::uint32_t { kWordBytes = 4 };

    static constexpr std::uint32_t kImageRows = 20;
    static constexpr std::uint32_t kCoreRows = 16;
    static constexpr std::uint32_t kRowBytes = 256;
    static constexpr std::uint32_t kRowWords = kRowBytes / kWordBytes;
    static constexpr std::uint32_t kImageEnd = kImageRows * kRowBytes;

    enum Fifo : std::uint32_t { kFifoEntries = 256 };

    static constexpr Reg image(std::size_t row, std::uint32_t w) noexcept {
        return static_cast<Reg>(row * kRowBytes + kWordBytes * w);
    }
};

static_assert(
    static_cast<std::uint32_t>(OsdBlock::image(OsdBlock::kImageRows - 1, OsdBlock::kRowWords - 1))
        == OsdBlock::kImageEnd - OsdBlock::kWordBytes,
    "the image is bytes 0x0000-0x13FF"
);
static_assert(
    OsdBlock::kImageEnd <= static_cast<std::uint32_t>(OsdBlock::Reg::Status),
    "STATUS follows the image"
);
static_assert(
    static_cast<std::size_t>(OsdBlock::Reg::Id) + OsdBlock::kWordBytes <= OsdBlock::kSize,
    "the ID ends inside the block"
);

}
