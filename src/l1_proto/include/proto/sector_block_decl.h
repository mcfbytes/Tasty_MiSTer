// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "hal/register_window.h"
#include "hal/sys_directory.h"
#include "infra/error.h"
#include "proto/sys_directory_image.h"

namespace mister::proto {

struct SectorBlockDecl {

    struct Card {
        static constexpr std::uint8_t kRead = 1u;
        static constexpr std::uint8_t kWrite = 2u;
        std::uint8_t slot = 0;
        std::uint8_t flags = 0;
        std::uint16_t frame_words = 0;
        std::uint32_t wsrc = 0;
        std::uint32_t extent = 0;

        [[nodiscard]] constexpr bool declared() const noexcept { return slot != 0 && wsrc != 0; }
    };

    static constexpr std::size_t kCards = 2;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint8_t slot = 0;
    std::uint16_t frame_words = 0;
    std::uint32_t cause = 0;
    std::uint16_t fifo_depth = 0;
    std::uint16_t strobe_gap = 0;
    std::uint16_t minor = 0;
    std::array<Card, kCards> cards{};
    DeclaredLine line_decl{};
};

[[nodiscard]] Ex<SectorBlockDecl> find_sector_block(
    const hal::RegisterWindow<hal::SysDirectory>& dir,
    std::uint32_t window_size
) noexcept;

[[nodiscard]] Ex<SectorBlockDecl> find_sector_block(
    const SysDirectoryImage& dir,
    std::uint32_t window_size
) noexcept;

}
