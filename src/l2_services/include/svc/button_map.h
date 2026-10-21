// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include "infra/error.h"
#include "svc/map_store.h"

namespace mister::svc {

using JoyWords = std::array<std::uint32_t, 32>;

struct ButtonMap {
    MapKind kind = MapKind::Joystick;
    std::vector<std::byte> blob;

    [[nodiscard]] Ex<JoyWords> words() const {
        if (kind != MapKind::Joystick || blob.size() != map_spec(MapKind::Joystick).bytes) {
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(blob.size())}
            );
        }
        JoyWords out{};
        for (std::size_t i = 0; i < out.size(); ++i) {
            std::uint32_t v = 0;
            for (std::size_t b = 0; b < 4; ++b) {
                v |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(blob[i * 4 + b]))
                    << (8u * b);
            }
            out[i] = v;
        }
        return out;
    }
};

}
