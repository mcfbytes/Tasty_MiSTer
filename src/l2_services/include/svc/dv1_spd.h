// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "proto/rotation_dir.h"
#include "svc/adv7513_spd_packet.h"

namespace mister::svc {

struct VideoSample;

enum class SpdForm : std::uint8_t { None, Standard, Dv1, Dv1Menu };

enum class SpdQuirk : std::uint8_t { Full = 0, NoStandard = 1, NoMenuResend = 2, Off = 3 };

struct SpdQuirkIni {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr SpdQuirk policy() const noexcept {
        constexpr auto kNoMenuResend = static_cast<std::uint8_t>(SpdQuirk::NoMenuResend);
        switch (v) {
            case 0:
                return SpdQuirk::Full;
            case 1:
                return SpdQuirk::NoStandard;
            case kNoMenuResend:
                return SpdQuirk::NoMenuResend;
            default:
                return SpdQuirk::Off;
        }
    }

    friend constexpr bool operator==(SpdQuirkIni, SpdQuirkIni) = default;
};

[[nodiscard]] constexpr SpdQuirk spd_quirk_of(SpdQuirkIni ini) noexcept {
    return ini.policy();
}

[[nodiscard]] constexpr SpdForm spd_form(bool direct_video, SpdQuirk quirk) noexcept {
    if (direct_video) {
        if (quirk == SpdQuirk::Full || quirk == SpdQuirk::NoStandard)
            return SpdForm::Dv1Menu;
        if (quirk == SpdQuirk::NoMenuResend)
            return SpdForm::Dv1;
        return SpdForm::None;
    }
    return quirk == SpdQuirk::Full ? SpdForm::Standard : SpdForm::None;
}

struct Dv1Facts {
    const VideoSample& sample;
    bool menu = false;
    proto::RotationDir dir{};
};

[[nodiscard]] adv7513::SpdPacket build_dv1_spd(const Dv1Facts& f, std::string_view name) noexcept;

}
