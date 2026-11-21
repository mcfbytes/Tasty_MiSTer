// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app {

class RoundWords final {
    TASTY_SEAT_EXEMPT(component);

public:
    constexpr void charge(proto::FioWordCount wrote) noexcept { since_take_ += wrote; }

    [[nodiscard]] constexpr proto::FioWordCount spent() const noexcept { return since_take_; }

    [[nodiscard]] constexpr proto::FioWordCount take() noexcept {
        return std::exchange(since_take_, proto::FioWordCount{});
    }

private:
    proto::FioWordCount since_take_{};
};

}
