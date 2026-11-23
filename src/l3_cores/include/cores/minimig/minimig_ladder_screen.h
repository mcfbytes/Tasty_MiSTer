// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "cores/minimig/minimig_ladder_bus_addr.h"
#include "cores/minimig/minimig_ladder_bus_bytes.h"
#include "cores/minimig/minimig_ladder_frames.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores::minimig {

class BootScreen {
    TASTY_SEAT_EXEMPT(component);

public:
    struct Assets {
        std::span<const std::byte> logo{};
        std::span<const std::byte> ball{};
        std::span<const std::byte> copper{};
    };

    static constexpr proto::FileSize kLogoBytes{0x680};
    static constexpr proto::FileSize kBallBytes{0x4000};
    static constexpr proto::FileSize kCopperBytes{0x35C};

    static constexpr std::size_t kColumns = 80;

    static constexpr BusBytes kRow{640 / 8};

    static void draw(FrameQueue& out, const Assets& assets);

    void print(FrameQueue& out, std::string_view text);

private:
    static constexpr BusAddr kTextHome = kBootScreen + kRow * 112u;

    BusAddr cursor_ = kTextHome;
};

}
