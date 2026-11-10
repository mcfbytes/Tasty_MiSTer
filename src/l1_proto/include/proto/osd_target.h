// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {

enum class OsdTarget : std::uint8_t {
    Hdmi = 1,
    Vga = 2,
    All = 3,
};

[[nodiscard]] constexpr bool includes_hdmi(OsdTarget set) noexcept {
    return set == OsdTarget::All || set == OsdTarget::Hdmi;
}

}
