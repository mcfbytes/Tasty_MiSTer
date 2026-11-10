// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/types.h"

namespace mister::svc {

struct VidPidKey {
    std::uint32_t v = 0;
    static constexpr unsigned kVidShift = 16;

    [[nodiscard]] static constexpr VidPidKey of(Vid vid, Pid pid) noexcept {
        return VidPidKey{
            (static_cast<std::uint32_t>(vid.v) << kVidShift) | static_cast<std::uint32_t>(pid.v)
        };
    }

    [[nodiscard]] static constexpr VidPidKey every_device() noexcept { return VidPidKey{1}; }

    friend constexpr bool operator==(VidPidKey, VidPidKey) = default;
};

static_assert(sizeof(VidPidKey) == sizeof(std::uint32_t), "the packed key is one word");
static_assert(
    VidPidKey::of(Vid{0x054C}, Pid{0x05C4}) == VidPidKey{0x054C05C4u},
    "vid occupies the high half"
);

}
