// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "hal/register_window.h"
#include "hal/sector_block.h"
#include "hal/sys_directory.h"
#include "hal/types.h"
#include "infra/error.h"
#include "os/map_length.h"
#include "os/types.h"
#include "proto/block_line.h"
#include "proto/declared_line.h"
#include "proto/dir_version.h"
#include "proto/sys_directory_image.h"
#include "proto/types.h"

namespace mister::proto {

struct SectorBlockDecl {

    struct Card {

        static constexpr BlockSize kMinFrameBytes{SdStatusWord::kMinBlockBytes};
        static constexpr BlockSize kMaxFrameBytes{
            hal::SectorBlock::kMaxFrameWords * hal::SectorBlock::kWordBytes
        };
        SlotIndex slot{};
        bool reads = false;
        bool writes = false;
        std::uint16_t frame_words = 0;
        os::PhysAddr wsrc{};
        os::MapLength extent{};

        [[nodiscard]] constexpr bool declared() const noexcept {
            return !slot.is_primary() && wsrc != os::PhysAddr{};
        }

        [[nodiscard]] constexpr BlockSize frame_bytes() const noexcept {
            return BlockSize{std::uint32_t{frame_words} * hal::SectorBlock::kWordBytes};
        }

        [[nodiscard]] constexpr bool frame_admitted() const noexcept {
            const BlockSize bytes = frame_bytes();
            return bytes >= kMinFrameBytes
                && bytes <= kMaxFrameBytes
                && bytes.power_of_two()
                && extent.holds_whole(bytes.map_length());
        }
    };

    static constexpr std::size_t kCards = 2;
    hal::LwOffset offset{};
    hal::LwExtent size{};
    SlotIndex slot{};
    std::uint16_t frame_words = 0;
    hal::LwOffset cause{};
    std::uint16_t fifo_depth = 0;
    std::uint16_t strobe_gap = 0;
    DirVersion version{};
    std::array<Card, kCards> cards{};

    [[nodiscard]] constexpr BlockSize frame_bytes() const noexcept {
        return BlockSize{std::uint32_t{frame_words} * hal::SectorBlock::kWordBytes};
    }
};

struct SectorBlockDeclLookup {
    SectorBlockDecl block{};
    DeclaredLine declared{};
};

static_assert(
    line_source(DirEntryKind::Sector) == LineFrom::IrqRow,
    "a SECTOR entry's line is the BLK row whose cause word 5 repeats"
);

static_assert(
    SectorBlockDecl{.frame_words = 64}.frame_bytes() == BlockSize{256},
    "a 64-word frame is a 256-byte block at the block word width"
);
static_assert(
    SectorBlockDecl::Card{.frame_words = 32}.frame_bytes() == BlockSize{128},
    "the card's frame is the same product, typed"
);

[[nodiscard]] Ex<SectorBlockDeclLookup> find_sector_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept;

}
