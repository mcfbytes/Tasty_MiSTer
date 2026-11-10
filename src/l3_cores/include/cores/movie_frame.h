// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>

#include "cores/movie_command_mask.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores {

inline constexpr std::size_t kMoviePorts = 4;

[[nodiscard]] constexpr svc::PlayerMask ports_without(
    svc::PlayerMask ports,
    svc::PlayerIndex slot
) noexcept {
    svc::PlayerMask out{};
    for (const svc::PlayerIndex p : svc::kPlayers)
        if (p != slot && ports.has(p))
            out = out.with(p);
    return out;
}

struct MovieFrame {
    MovieCommandMask commands{};
    std::array<proto::JoyMask, kMoviePorts> mask{};
};

}
