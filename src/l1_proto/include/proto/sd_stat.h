// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"

namespace mister::proto {

struct SdStat {
    bool valid = false;
    BlockCount block_count{};
    BlockSize block_size{};
    SlotIndex slot{};
    bool wr = false;
    bool rd = false;

    [[nodiscard]] constexpr bool is_one_block() const noexcept {
        return block_count == BlockCount{1u};
    }

    [[nodiscard]] constexpr bool reads_one(SlotIndex s) const noexcept {
        return valid && slot == s && rd && !wr && is_one_block();
    }

    static constexpr SdStat of(SdStatusWord word) noexcept {
        return SdStat{
            .valid = word.valid(),
            .block_count = word.block_count(),
            .block_size = word.block_size(),
            .slot = word.slot(),
            .wr = word.writes(),
            .rd = word.decoded() && !word.writes()
        };
    }
};

static_assert(
    SdStat::of(SdStatusWord{0x80CAu}).slot == SlotIndex{2}
        && SdStat::of(SdStatusWord{0x80CAu}).wr
        && !SdStat::of(SdStatusWord{0x80CAu}).rd
        && SdStat::of(SdStatusWord{0x80CAu}).is_one_block()
        && SdStat::of(SdStatusWord{0x80CAu}).block_size == BlockSize{1024},
    "PSX card 1's one-block write: slot 2, 1 KB"
);
static_assert(
    SdStat::of(SdStatusWord{0x80C5u}).reads_one(SlotIndex{1})
        && !SdStat::of(SdStatusWord{0x80C5u}).reads_one(SlotIndex{0})
        && !SdStat::of(SdStatusWord{0x80C6u}).reads_one(SlotIndex{1})
        && !SdStat::of(SdStatusWord{0x82C5u}).reads_one(SlotIndex{1}),
    "PSX slot 1's one-block read, and nothing else"
);

}
