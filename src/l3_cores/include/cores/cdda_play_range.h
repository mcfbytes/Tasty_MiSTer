// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/types.h"
#include "infra/seat.h"

namespace mister::cores {

class CddaPlayRange {
    TASTY_SEAT_RESIDENT(RT);

public:
    void play(HeadLba from, HeadLba disc_end) noexcept {
        start_ = from;
        end_ = disc_end;
    }

    void set_end(HeadLba end) noexcept { end_ = end; }

    void clear() noexcept { start_ = end_ = HeadLba{}; }

    [[nodiscard]] HeadLba start() const noexcept { return start_; }

    [[nodiscard]] HeadLba end() const noexcept { return end_; }

    [[nodiscard]] bool past_end(HeadLba lba) const noexcept { return lba > end_; }

private:
    HeadLba start_{};
    HeadLba end_{};
};

}
