// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "infra/affine.h"
#include "proto/types.h"

namespace mister::proto {

struct TrackShape {

    BlockSize unit{};

    std::array<std::uint8_t, 4> counts{};

    struct Ask {
        Lba word{};
        BlockCount units{};
        friend constexpr bool operator==(const Ask&, const Ask&) = default;
    };

    [[nodiscard]] constexpr bool declared() const noexcept { return !unit.empty(); }

    [[nodiscard]] constexpr std::optional<Ask> ask(Lba word, BlockCount blks) const noexcept {
        if (!declared())
            return std::nullopt;
        if (blks == BlockCount{})
            return std::nullopt;
        for (const std::uint8_t count : counts) {
            if (blks == BlockCount{count})
                return Ask{word, blks - BlockCount{1}};
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr FileSize payload_bytes(const Ask& a) const noexcept {
        return unit.bytes_of(a.units);
    }

    [[nodiscard]] static constexpr FileOffset written_word(const Ask& a) noexcept {
        return kWord.position_of(a.word);
    }

    [[nodiscard]] constexpr Ask written_ask(FileOffset word, FileSize payload) const noexcept {
        return Ask{Lba{} + kWord.blocks_in(word - kFileStart), unit.blocks_in(payload)};
    }

    friend constexpr bool operator==(const TrackShape&, const TrackShape&) = default;

private:
    static const BlockSize kWord;
};

inline constexpr BlockSize TrackShape::kWord{1};

static_assert(
    sizeof(TrackShape) == 8 && std::has_unique_object_representations_v<TrackShape>,
    "a mount's answer carries it beside the extent, with no hidden padding"
);
static_assert(
    TrackShape::written_word({.word = Lba{0x0423}, .units = BlockCount{52}})
        == kFileStart + FileSize{0x0423},
    "a track write's request carries the address word verbatim, never a position"
);

}
