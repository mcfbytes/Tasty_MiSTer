// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::ui {

class ShowEdgeLatch final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    [[nodiscard]] bool on_open(bool open_now) noexcept;

    void mark_closed() noexcept { was_open_ = false; }

    [[nodiscard]] std::uint32_t edges() const noexcept { return edges_; }

private:
    bool was_open_ = false;
    std::uint32_t edges_ = 0;
};

}
