// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>

#include "cores/movie_command_mask.h"
#include "infra/indexed_array.h"
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

    [[nodiscard]] static constexpr bool has_port(proto::PlayerIndex port) noexcept {
        return port < proto::PlayerIndex::of(kMoviePorts);
    }

    [[nodiscard]] constexpr MovieFrame with_pad(
        proto::PlayerIndex port,
        proto::JoyMask pressed
    ) const noexcept {
        if (!has_port(port))
            return *this;
        MovieFrame out = *this;
        infra::at_index(out.mask, port) = infra::at_index(out.mask, port).with(pressed);
        return out;
    }
};

}
