// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores::minimig {

template <class Kind>
struct ShareKey {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr ShareKey next() const noexcept {
        return ShareKey{v >= kLast ? 1u : v + 1u};
    }

    [[nodiscard]] constexpr bool is_root() const noexcept { return v == 0; }

    friend constexpr bool operator==(ShareKey, ShareKey) = default;
    friend constexpr auto operator<=>(ShareKey, ShareKey) = default;

private:
    static constexpr std::uint32_t kLast = 0x7FFF'FFFFu;
};

enum class ShareLockKind : std::uint8_t {};
enum class ShareFileKind : std::uint8_t {};
using ShareLockKey = ShareKey<ShareLockKind>;
using ShareFileKey = ShareKey<ShareFileKind>;

static_assert(ShareLockKey{0x7FFF'FFFFu}.next() == ShareLockKey{1});
static_assert(ShareLockKey{1}.next() == ShareLockKey{2});

}
