// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <vector>

#include "infra/endian.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "svc/joy_bit.h"
#include "svc/map_store.h"
#include "svc/packed_keys.h"
#include "svc/sys_button.h"
#include "svc/types.h"

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

[[nodiscard]] inline std::span<const std::byte> as_bytes(const SysWords& words) noexcept {
    return std::as_bytes(std::span<const PackedKeys>{words.begin(), words.size()});
}

[[nodiscard]] inline std::span<const std::byte> as_bytes(const WireWords& words) noexcept {
    return std::as_bytes(std::span<const PackedKeys>{words.begin(), words.size()});
}

[[nodiscard]] inline WireWords as_wire_words(const SysWords& words) noexcept {
    WireWords out{};
    std::memcpy(&out, as_bytes(words).data(), sizeof out);
    return out;
}

[[nodiscard]] inline SysWords as_sys_words(const WireWords& words) noexcept {
    SysWords out{};
    std::memcpy(&out, as_bytes(words).data(), sizeof out);
    return out;
}

struct ButtonMap {
    MapKind kind = MapKind::Joystick;
    std::vector<std::byte> blob;

    [[nodiscard]] Ex<WireWords> words() const {
        if (kind != MapKind::Joystick || blob.size() != map_spec(MapKind::Joystick).bytes) {
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(blob.size())}
            );
        }
        constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
        WireWords out{};
        std::span<const std::byte> in = blob;
        JoyBit bit{};
        while (!in.empty()) {
            out[bit] = packed_keys_of(infra::load_le<std::uint32_t>(in.first<kWordBytes>()));
            in = in.subspan(kWordBytes);
            bit = bit.next();
        }
        return out;
    }
};

}
