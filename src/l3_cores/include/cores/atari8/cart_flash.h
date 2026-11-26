// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/types.h"

namespace mister::cores::atari8 {

struct CartFlash {
    hal::LinkWord v{};

    [[nodiscard]] constexpr bool written() const noexcept { return v.intersects(kWritten); }

    [[nodiscard]] constexpr bool second() const noexcept { return v.intersects(kSecond); }

    [[nodiscard]] constexpr bool autosave() const noexcept { return v.intersects(kAutosave); }

    [[nodiscard]] constexpr bool save_now() const noexcept { return v.intersects(kSaveNow); }

    [[nodiscard]] constexpr bool boots_on_mount() const noexcept { return v.intersects(kBoots); }

    static constexpr hal::LinkWord kWritten{0x0001};
    static constexpr hal::LinkWord kSecond{0x0002};
    static constexpr hal::LinkWord kAutosave{0x0004};
    static constexpr hal::LinkWord kSaveNow{0x0008};
    static constexpr hal::LinkWord kBoots{0x0010};
};

}
