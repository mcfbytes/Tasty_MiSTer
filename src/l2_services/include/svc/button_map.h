// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "infra/endian.h"
#include "infra/error.h"
#include "svc/map_store.h"
#include "svc/packed_keys.h"

namespace mister::svc {

[[nodiscard]] constexpr std::uint32_t packed_word(PackedKeys k) noexcept {
    constexpr unsigned kHalfBits = 16;
    return static_cast<std::uint32_t>(k.lo.v) | (static_cast<std::uint32_t>(k.hi.v) << kHalfBits);
}

[[nodiscard]] constexpr PackedKeys packed_keys_of(std::uint32_t w) noexcept {
    constexpr unsigned kHalfBits = 16;
    return PackedKeys{
        EvdevCode{static_cast<std::uint16_t>(w)},
        EvdevCode{static_cast<std::uint16_t>(w >> kHalfBits)}
    };
}

static_assert(packed_word(packed_keys_of(0x01230131u)) == 0x01230131u, "the word round-trips");

inline constexpr std::size_t kJoyWords = 32;
using JoyWords = std::array<PackedKeys, kJoyWords>;
static_assert(sizeof(JoyWords) == 128, "req: the Joystick blob is exactly 128 bytes");
static_assert(JoyWords{}.size() == kJoyWords, "the word count names the array's extent");

struct ButtonMap {
    MapKind kind = MapKind::Joystick;
    std::vector<std::byte> blob;

    [[nodiscard]] Ex<JoyWords> words() const {
        if (kind != MapKind::Joystick || blob.size() != map_spec(MapKind::Joystick).bytes) {
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(blob.size())}
            );
        }
        constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
        JoyWords out{};
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = packed_keys_of(
                infra::load_le<std::uint32_t>(
                    std::span<const std::byte>(blob).subspan(i * kWordBytes).first<kWordBytes>()
                )
            );
        }
        return out;
    }
};

}
