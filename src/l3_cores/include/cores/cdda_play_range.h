// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::cores {

class CddaPlayRange {
    TASTY_SEAT_RESIDENT(RT);

public:
    void play(std::int32_t from, std::int32_t disc_end) noexcept {
        start_ = from;
        end_ = disc_end;
    }

    void set_end(std::int32_t end) noexcept { end_ = end; }

    void clear() noexcept { start_ = end_ = 0; }

    [[nodiscard]] std::int32_t start() const noexcept { return start_; }

    [[nodiscard]] std::int32_t end() const noexcept { return end_; }

    [[nodiscard]] bool past_end(std::int32_t lba) const noexcept { return lba > end_; }

private:
    std::int32_t start_ = 0;
    std::int32_t end_ = 0;
};

}
