// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/types.h"

namespace mister::proto {

inline constexpr std::chrono::steady_clock::time_point kNoDue{};

[[nodiscard]] constexpr std::chrono::steady_clock::time_point due_after(
    std::int64_t now_ns,
    std::int64_t span_ns
) noexcept {
    return std::chrono::steady_clock::time_point{
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::nanoseconds{now_ns + span_ns}
        )
    };
}

[[nodiscard]] constexpr std::int64_t due_ns_of(std::chrono::steady_clock::time_point at) noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(at.time_since_epoch()).count();
}

struct SlotRecord {
    TASTY_SEAT_RESIDENT(RT);

    struct Slot {
        enum class State : std::uint8_t { Empty, Mounted };
        State state = State::Empty;
        BlockSize block_size{kDefaultBlockBytes};
        FileSize size_bytes{};

        FileSize file_bytes{};
        PathId path_id{};

        bool writable = false;
        bool growable = false;
        bool deferred_create = false;
    };

    struct Window {
        Lba base{};
        std::uint32_t valid_bytes = 0;
        BlockSize block_size{};
        ArenaHalf half{};

        [[nodiscard]] bool covers(Lba lba, BlockCount blocks, BlockCount capacity) const noexcept {
            return lba >= base && blocks <= capacity && (lba - base) <= (capacity - blocks);
        }
    };

    struct SlotGate {
        enum class State : std::uint8_t { Idle, Fetching, Committing, Staging };
        State state = State::Idle;
        StorageSeq seq{};
        std::uint32_t epoch = 0;
        Lba base{};
        BlockSize block_size{};
        std::uint64_t offset = 0;
        std::uint32_t len = 0;

        std::chrono::steady_clock::time_point due{};
        bool answer_owed = false;
        bool blank_next = false;
    };

    static constexpr std::uint8_t kNoHalf = ArenaHalf::kCount;

    Slot slot{};
    Window window{};
    SlotGate gate{};

    std::uint8_t half_busy = 0;
    std::uint32_t epoch = 0;

    std::chrono::steady_clock::time_point no_half_due{};

    std::chrono::steady_clock::time_point resident_due{};

    [[nodiscard]] bool half_held(ArenaHalf h) const noexcept {
        return (half_busy & h.mask<std::uint8_t>()) != 0;
    }

    void lend_half(ArenaHalf h) noexcept { half_busy |= h.mask<std::uint8_t>(); }

    void reclaim_half(ArenaHalf h) noexcept {
        half_busy &= static_cast<std::uint8_t>(~h.mask<std::uint8_t>());
    }

    [[nodiscard]] ArenaHalf fill_half() const noexcept {
        const ArenaHalf other = window.half.other();
        if (!half_held(other))
            return other;
        const ArenaHalf served = window.half;
        if (!half_held(served))
            return served;
        return ArenaHalf{kNoHalf};
    }

    void drop_window() noexcept {
        window = Window{};
        gate.blank_next = false;
        resident_due = kNoDue;
        no_half_due = kNoDue;
        ++epoch;
    }

    void finish_gate(bool blank_next) noexcept {
        gate = SlotGate{};
        gate.blank_next = blank_next;
    }
};

}
