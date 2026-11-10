// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "hal/register_window.h"
#include "hal/sector_block.h"
#include "hal/sys_directory.h"
#include "infra/error.h"
#include "proto/declared_line.h"
#include "proto/sys_directory_image.h"
#include "proto/types.h"

namespace mister::proto {

struct SectorBlockDecl {

    struct Card {

        static constexpr std::uint32_t kMinFrameBytes = SdStatusWord::kMinBlockBytes;
        static constexpr std::uint32_t kMaxFrameBytes = hal::SectorBlock::kMaxFrameWords
            * hal::SectorBlock::kWordBytes;
        SlotIndex slot{};
        bool reads = false;
        bool writes = false;
        std::uint16_t frame_words = 0;
        std::uint32_t wsrc = 0;
        std::uint32_t extent = 0;

        [[nodiscard]] constexpr bool declared() const noexcept { return slot.v != 0 && wsrc != 0; }

        [[nodiscard]] constexpr BlockSize frame_bytes() const noexcept {
            return BlockSize{std::uint32_t{frame_words} * hal::SectorBlock::kWordBytes};
        }

        [[nodiscard]] constexpr bool frame_admitted() const noexcept {
            const std::uint32_t bytes = std::uint32_t{frame_words} * hal::SectorBlock::kWordBytes;
            return bytes >= kMinFrameBytes
                && bytes <= kMaxFrameBytes
                && (bytes & (bytes - 1u)) == 0u
                && extent >= bytes
                && extent % bytes == 0u;
        }
    };

    static constexpr std::size_t kCards = 2;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    SlotIndex slot{};
    std::uint16_t frame_words = 0;
    std::uint32_t cause = 0;
    std::uint16_t fifo_depth = 0;
    std::uint16_t strobe_gap = 0;
    std::uint16_t minor = 0;
    std::array<Card, kCards> cards{};
    DeclaredLine line_decl{};

    [[nodiscard]] constexpr BlockSize frame_bytes() const noexcept {
        return BlockSize{std::uint32_t{frame_words} * hal::SectorBlock::kWordBytes};
    }
};

static_assert(
    SectorBlockDecl{.frame_words = 64}.frame_bytes() == BlockSize{256},
    "a 64-word frame is a 256-byte block at the block word width"
);
static_assert(
    SectorBlockDecl::Card{.frame_words = 32}.frame_bytes() == BlockSize{128},
    "the card's frame is the same product, typed"
);

[[nodiscard]] Ex<SectorBlockDecl> find_sector_block(
    const SysDirectoryImage& dir,
    std::uint32_t window_size
) noexcept;

}
