// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/seat.h"
#include "os/raw_monotonic_clock.h"
#include "proto/block_geometry.h"
#include "proto/storage_request.h"
#include "proto/types.h"

namespace mister::proto {

inline constexpr os::RawMonotonicClock::time_point kNoDue{};

[[nodiscard]] constexpr os::RawMonotonicClock::time_point due_after(
    os::RawMonotonicClock::time_point now,
    std::chrono::nanoseconds span
) noexcept {
    return now + span;
}

struct SlotRecord {
    TASTY_SEAT_RESIDENT(RT);

    struct Slot {
        enum class State : std::uint8_t { Empty, Mounted, Changing };
        State state = State::Empty;
        BlockSize block_size{kDefaultBlockBytes};
        FileSize size_bytes{};

        FileSize file_bytes{};
        PathId path_id{};

        bool writable = false;
        bool growable = false;
        bool deferred_create = false;
    };

    struct Credit {
        enum class Phase : std::uint8_t { Live, Expired, Rearmed };
        StorageRequest::Kind kind{};
        MountEpoch epoch{};
        os::RawMonotonicClock::time_point due = kNoDue;
        Phase phase = Phase::Live;

        [[nodiscard]] bool overdue(os::RawMonotonicClock::time_point now) const noexcept {
            return now >= due;
        }

        [[nodiscard]] bool stale(MountEpoch current) const noexcept {
            return phase != Phase::Live || epoch != current;
        }
    };

    Slot slot{};
    MountEpoch epoch{};
    Credit data{}, ctl{};

    os::RawMonotonicClock::time_point change_due = kNoDue;
};

}
