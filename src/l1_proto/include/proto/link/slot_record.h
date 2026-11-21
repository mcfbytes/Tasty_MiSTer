// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/niche.h"
#include "infra/seat.h"
#include "os/deadline.h"
#include "os/raw_monotonic_clock.h"
#include "proto/block_geometry.h"
#include "proto/slot_attributes.h"
#include "proto/storage_request.h"
#include "proto/types.h"

namespace mister::proto::link {

[[nodiscard]] constexpr os::Deadline due_after(
    os::RawMonotonicClock::time_point now,
    std::chrono::nanoseconds span
) noexcept {
    return os::Deadline::at_point(now + span);
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
        MountClass mount = MountClass::Image;
    };

    struct Credit {
        enum class Phase : std::uint8_t { Live, Expired, Rearmed };
        StorageRequest::Kind kind{};
        MountEpoch epoch{};
        infra::Niche<os::Deadline> due{};
        Phase phase = Phase::Live;

        [[nodiscard]] bool overdue(os::RawMonotonicClock::time_point now) const noexcept {
            return !due.has_value() || now >= due->at();
        }

        [[nodiscard]] bool stale(MountEpoch current) const noexcept {
            return phase != Phase::Live || epoch != current;
        }
    };

    Slot slot{};
    MountEpoch epoch{};
    Credit data{}, ctl{};

    infra::Niche<os::Deadline> change_due{};
};

}
