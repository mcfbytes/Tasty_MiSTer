// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/types.h"

namespace mister::proto {

struct SlotRecord {
    TASTY_SEAT_RESIDENT(RT);

    struct Slot {
        enum class State : std::uint8_t { Empty, Mounted };
        State state = State::Empty;
        std::uint32_t block_size = 512;
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
        std::uint32_t block_size = 0;
        ArenaHalf half{};
    };

    struct SlotGate {
        enum class State : std::uint8_t { Idle, Fetching, Committing, Staging };
        State state = State::Idle;
        StorageSeq seq{};
        std::uint32_t epoch = 0;
        Lba base{};
        std::uint32_t block_size = 0;
        std::uint64_t offset = 0;
        std::uint32_t len = 0;
        std::int64_t due_ns = 0;
        bool answer_owed = false;
        bool blank_next = false;
    };

    static constexpr std::uint8_t kNoHalf = 2;

    Slot slot{};
    Window window{};
    SlotGate gate{};

    std::uint8_t half_busy = 0;
    std::uint32_t epoch = 0;

    std::int64_t no_half_due_ns = 0;

    std::int64_t resident_due_ns = 0;

    [[nodiscard]] bool half_held(ArenaHalf h) const noexcept {
        return (half_busy & static_cast<std::uint8_t>(1u << h.v)) != 0;
    }

    [[nodiscard]] ArenaHalf fill_half() const noexcept {
        const ArenaHalf other{static_cast<std::uint8_t>(window.half.v ^ 1u)};
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
        resident_due_ns = 0;
        no_half_due_ns = 0;
        ++epoch;
    }

    void finish_gate(bool blank_next) noexcept {
        gate = SlotGate{};
        gate.blank_next = blank_next;
    }
};

}
