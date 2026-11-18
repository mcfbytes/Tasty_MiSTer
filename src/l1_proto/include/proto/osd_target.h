// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

namespace mister::proto {

enum class OsdTarget : std::uint8_t {
    Hdmi = 1,
    Vga = 2,
    All = 3,
};

[[nodiscard]] constexpr bool includes_hdmi(OsdTarget set) noexcept {
    return set == OsdTarget::All || set == OsdTarget::Hdmi;
}

[[nodiscard]] constexpr bool includes_vga(OsdTarget set) noexcept {
    return set == OsdTarget::All || set == OsdTarget::Vga;
}

[[nodiscard]] constexpr std::optional<OsdTarget> osd_target_meet(
    OsdTarget a,
    OsdTarget b
) noexcept {
    const bool hdmi = includes_hdmi(a) && includes_hdmi(b);
    const bool vga = includes_vga(a) && includes_vga(b);
    if (hdmi && vga)
        return OsdTarget::All;
    if (hdmi)
        return OsdTarget::Hdmi;
    if (vga)
        return OsdTarget::Vga;
    return std::nullopt;
}

}
