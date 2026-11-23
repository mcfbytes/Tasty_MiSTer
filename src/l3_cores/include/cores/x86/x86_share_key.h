// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <span>

#include "infra/endian.h"

namespace mister::cores::x86 {

template <class Kind>
struct ShareKey {
    static constexpr std::uint16_t kFirst = 1;
    static constexpr std::uint16_t kLast = std::numeric_limits<std::uint16_t>::max();

    std::uint16_t v = 0;

    [[nodiscard]] static constexpr ShareKey decode(std::span<const std::byte> bytes) noexcept {
        if (bytes.size() < sizeof(std::uint16_t))
            return {};
        return ShareKey{infra::load_le<std::uint16_t>(bytes.first<sizeof(std::uint16_t)>())};
    }

    [[nodiscard]] constexpr ShareKey next() const noexcept {
        return ShareKey{v == kLast ? kFirst : static_cast<std::uint16_t>(v + 1u)};
    }

    constexpr void store(std::span<std::byte, 2> out) const noexcept {
        infra::store_le<std::uint16_t>(out, v);
    }

    friend constexpr bool operator==(ShareKey, ShareKey) = default;
    friend constexpr auto operator<=>(ShareKey, ShareKey) = default;
};

enum class ShareLockKind : std::uint8_t {};
enum class ShareFileKind : std::uint8_t {};
using ShareLockKey = ShareKey<ShareLockKind>;
using ShareFileKey = ShareKey<ShareFileKind>;

}
