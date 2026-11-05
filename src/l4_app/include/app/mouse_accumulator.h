// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <cstdint>

#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app {

struct MouseDelta {
    std::int32_t dx = 0, dy = 0, dz = 0;
    proto::MouseButtons buttons{};
    bool button_edge = false;
    bool moved = false;
};

class MouseAccumulator {
    TASTY_SEAT_EXEMPT(component);

    struct Total {
        std::uint32_t v = 0;

        [[nodiscard]] constexpr Total plus(std::int32_t delta) const noexcept {
            return Total{v + std::bit_cast<std::uint32_t>(delta)};
        }

        [[nodiscard]] constexpr Total bump() const noexcept { return Total{v + 1u}; }

        [[nodiscard]] constexpr std::int32_t since(Total mark) const noexcept {
            return std::bit_cast<std::int32_t>(v - mark.v);
        }

        friend constexpr bool operator==(Total, Total) = default;
    };

    Total x_{}, y_{}, wheel_{};
    Total edges_{};
    proto::MouseButtons buttons_{};
    Total wm_x_{}, wm_y_{}, wm_w_{}, wm_edges_{};

public:
    void accumulate(std::int32_t rx, std::int32_t ry, std::int32_t rw) noexcept {
        x_ = x_.plus(rx);
        y_ = y_.plus(ry);
        wheel_ = wheel_.plus(rw);
    }

    void note_buttons(proto::MouseButtons level) noexcept {
        if (level != buttons_) {
            buttons_ = level;
            edges_ = edges_.bump();
        }
    }

    MouseDelta take() noexcept {
        MouseDelta d{};
        d.dx = x_.since(wm_x_);
        d.dy = y_.since(wm_y_);
        d.dz = wheel_.since(wm_w_);
        d.buttons = buttons_;
        d.button_edge = (edges_ != wm_edges_);
        d.moved = (d.dx != 0) || (d.dy != 0) || (d.dz != 0);
        wm_x_ = x_;
        wm_y_ = y_;
        wm_w_ = wheel_;
        wm_edges_ = edges_;
        return d;
    }
};

}
