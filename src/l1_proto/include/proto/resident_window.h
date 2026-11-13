// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "proto/block_geometry.h"
#include "proto/types.h"

namespace mister::proto {

struct ResidentWindow {
    enum class Source : std::uint8_t { Image, Substitute };

    struct Ask {
        Lba lba{};
        BlockCount blocks{};
        std::optional<BlockSize> geometry{};
    };

    struct Hit {
        enum class Is : std::uint8_t { Miss, Image, Substitute };
        Is is = Is::Miss;
        std::span<const std::uint8_t> bytes{};
        std::optional<Lba> read_ahead{};
    };

    Lba base{};
    BlockSize block_size{};
    BlockSize valid{};
    Source source = Source::Image;

    [[nodiscard]] static constexpr ResidentWindow image(
        Lba base,
        BlockSize geometry,
        BlockSize valid
    ) noexcept {
        return ResidentWindow{
            .base = base,
            .block_size = geometry,
            .valid = valid,
            .source = Source::Image
        };
    }

    [[nodiscard]] static constexpr ResidentWindow substitute() noexcept {
        return ResidentWindow{.source = Source::Substitute};
    }

    [[nodiscard]] constexpr bool covers(
        Lba lba,
        BlockCount blocks,
        BlockCount capacity
    ) const noexcept {
        return lba >= base && blocks <= capacity && (lba - base) <= (capacity - blocks);
    }

    [[nodiscard]] Hit hit(const Ask& ask, std::span<const std::uint8_t> bytes) const noexcept {
        if (source == Source::Substitute) {
            return Hit{.is = Hit::Is::Substitute};
        }
        if (valid.v == 0)
            return Hit{};
        if (ask.geometry && *ask.geometry != block_size)
            return Hit{};
        const BlockCount capacity = block_size.blocks_in(FileSize{kBlockStagingBytes});
        if (!covers(ask.lba, ask.blocks, capacity))
            return Hit{};
        const std::size_t offset = block_size.offset_of(ask.lba - base);
        const std::size_t length = block_size.length_of(ask.blocks);
        if (offset + length > bytes.size())
            return Hit{};
        const Lba next = ask.lba + ask.blocks;
        const bool at_end = (next - base) == capacity;
        return Hit{
            .is = Hit::Is::Image,
            .bytes = bytes.subspan(offset, length),
            .read_ahead = at_end ? std::optional<Lba>{next} : std::nullopt
        };
    }
};

}
