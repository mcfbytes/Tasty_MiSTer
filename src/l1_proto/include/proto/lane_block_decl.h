// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include "hal/register_window.h"
#include "hal/sys_directory.h"
#include "infra/error.h"
#include "proto/declared_line.h"
#include "proto/sys_directory_image.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

struct LaneBlockDecl {
    static constexpr std::uint32_t kKind = 5;
    static constexpr std::uint32_t kMajor = 1;

    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::array<std::uint32_t, 8> ops{};
    std::uint32_t caps = 0;
    std::uint16_t fifo_depth = 0;
    std::uint16_t strobe_gap = 0;
    DeclaredLine line_decl{};

    [[nodiscard]] constexpr bool honours(UioOp op) const noexcept {
        const auto o = proto::word(op).v;
        constexpr std::uint32_t kWordBits = std::numeric_limits<std::uint32_t>::digits;
        return o <= std::numeric_limits<std::uint8_t>::max()
            && ((ops[o / kWordBits] >> (o % kWordBits)) & 1u) != 0u;
    }
};

[[nodiscard]] Ex<LaneBlockDecl> find_lane_block(
    const SysDirectoryImage& dir,
    std::uint32_t window_size
) noexcept;
[[nodiscard]] Ex<LaneBlockDecl> find_lane_block(
    const hal::RegisterWindow<hal::SysDirectory>& dir,
    std::uint32_t window_size
) noexcept;

}
