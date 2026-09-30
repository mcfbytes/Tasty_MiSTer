// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

struct VideoSample {
    std::uint16_t res = 0;
    std::uint32_t width = 0, height = 0;
    std::uint32_t htime = 0, vtime = 0, ptime = 0, vtimeh = 0, ctime = 0;
    std::uint16_t pixrep = 0, de_h = 0, de_v = 0;
    std::uint32_t frame_clocks = 0;

    std::uint8_t fb_crc = 0;
    std::uint16_t arx_raw = 0, ary_raw = 0;
    std::uint16_t fb_fmt = 0, fb_width = 0, fb_height = 0;

    std::uint16_t flt_flags = 0;

    bool interlaced() const noexcept { return (res & 0x100u) != 0; }
    bool rotated() const noexcept { return (res & 0x200u) != 0; }
    bool arxy() const noexcept { return (arx_raw & 0x1000u) != 0; }
    std::uint16_t arx() const noexcept { return static_cast<std::uint16_t>(arx_raw & 0x0FFFu); }
    std::uint16_t ary() const noexcept { return static_cast<std::uint16_t>(ary_raw & 0x0FFFu); }
    bool fb_en() const noexcept { return (fb_fmt & 0x40u) != 0; }
};

}  // namespace mister::svc
