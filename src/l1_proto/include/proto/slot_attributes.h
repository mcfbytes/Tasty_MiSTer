// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

enum class MountClass : std::uint8_t { Image, Save };

struct SlotAttributes {
    bool writable = false;
    bool growable = false;
    bool deferred_create = false;
    MountClass mount = MountClass::Image;

    std::uint64_t channel_bytes = 0;

    [[nodiscard]] static constexpr SlotAttributes at_mount(MountClass mount, bool exists) noexcept {
        const bool save = mount == MountClass::Save;
        return SlotAttributes{
            .writable = save || exists,
            .growable = save,
            .deferred_create = save && !exists,
            .mount = mount
        };
    }
};

}
