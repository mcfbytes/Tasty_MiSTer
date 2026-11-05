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
#include "infra/seat.h"

namespace mister::proto {

struct AxiBlockDecl {
    TASTY_SEAT_EXEMPT(const_shared);
    static constexpr std::uint32_t kPadKind = 1;
    static constexpr std::uint32_t kPadMajor = 1;
    static constexpr std::uint32_t kSectorKind = 2;
    static constexpr std::uint32_t kSectorMajor = 1;
    static constexpr std::uint32_t kOsdKind = 3;
    static constexpr std::uint32_t kOsdMajor = 1;
    static constexpr std::uint32_t kSysKind = 4;
    static constexpr std::uint32_t kSysMajor = 1;

    std::uint32_t kind = 0;
    std::uint32_t version = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::array<std::uint32_t, 12> ops{};
    DeclaredLine line{};

    [[nodiscard]] constexpr bool honours(UioOp op) const noexcept {
        const auto o = proto::word(op).v;
        constexpr std::uint32_t kWordBits = std::numeric_limits<std::uint32_t>::digits;
        return o <= std::numeric_limits<std::uint8_t>::max()
            && ((ops[o / kWordBits] >> (o % kWordBits)) & 1u) != 0u;
    }
};

[[nodiscard]] Ex<AxiBlockDecl> find_pad_block(
    const hal::RegisterWindow<hal::SysDirectory>& dir,
    std::uint32_t window_size
) noexcept;

[[nodiscard]] Ex<AxiBlockDecl> find_pad_block(
    const SysDirectoryImage& dir,
    std::uint32_t window_size
) noexcept;

}
