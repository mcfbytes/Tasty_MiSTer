// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/resolution.h"

namespace mister::svc {

struct VideoSample {
    std::uint16_t res = 0;
    Resolution active{};
    std::uint32_t htime = 0, vtime = 0, ptime = 0, vtimeh = 0, ctime = 0;
    std::uint16_t pixrep = 0, de_h = 0, de_v = 0;
    std::uint32_t frame_clocks = 0;

    std::uint8_t fb_crc = 0;
    std::uint16_t arx_raw = 0, ary_raw = 0;
    std::uint16_t fb_fmt = 0, fb_width = 0, fb_height = 0;

    std::uint16_t flt_flags = 0;

    bool interlaced() const noexcept {
        constexpr std::uint16_t kResInterlaced = 0x100;
        return (res & kResInterlaced) != 0;
    }

    bool rotated() const noexcept {
        constexpr std::uint16_t kResRotated = 0x200;
        return (res & kResRotated) != 0;
    }

    bool arxy() const noexcept {
        constexpr std::uint16_t kArxyFlag = 0x1000;
        return (arx_raw & kArxyFlag) != 0;
    }

    std::uint16_t arx() const noexcept {
        constexpr std::uint16_t kArValueMask = 0x0FFF;
        return static_cast<std::uint16_t>(arx_raw & kArValueMask);
    }

    std::uint16_t ary() const noexcept {
        constexpr std::uint16_t kArValueMask = 0x0FFF;
        return static_cast<std::uint16_t>(ary_raw & kArValueMask);
    }

    bool fb_en() const noexcept {
        constexpr std::uint16_t kFbEnable = 0x40;
        return (fb_fmt & kFbEnable) != 0;
    }
};

}
