// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

#include "hal/spi_halves.h"
#include "hal/types.h"
#include "svc/video/aspect_raw.h"
#include "svc/video/fb_par_crc.h"
#include "svc/video/resolution.h"
#include "svc/video/res_word.h"
#include "svc/types.h"

namespace mister::svc::video {

struct VideoSample {
    ResWord res{};
    Resolution active{};
    std::uint32_t htime = 0, vtime = 0, ptime = 0, vtimeh = 0, ctime = 0;
    std::uint16_t pixrep = 0, de_h = 0, de_v = 0;
    std::uint32_t frame_clocks = 0;

    FbParCrc fb_crc{};
    AspectRaw aspect{};
    std::uint16_t fb_fmt = 0, fb_width = 0, fb_height = 0;

    FltFlags flt_flags{};

    static constexpr std::size_t kVresRunWords = 19;

    static constexpr std::size_t kFbParRunWords = 5;

    [[nodiscard]] constexpr VideoSample with_vres_run(
        std::span<const hal::SpiWord, kVresRunWords> run
    ) const noexcept {
        VideoSample out = *this;
        auto w = run.begin();
        const auto halves = [&w] {
            const hal::SpiWord low = *w++;
            return hal::SpiHalves{.low = low, .high = *w++};
        };

        constexpr auto dword = [](std::uint32_t bits) noexcept {
            return bits;
        };
        out.active.width = halves().joined(dword);
        out.active.height = halves().joined(dword);
        out.htime = halves().joined(dword);
        out.vtime = halves().joined(dword);
        out.ptime = halves().joined(dword);
        out.vtimeh = halves().joined(dword);
        out.ctime = halves().joined(dword);
        out.pixrep = (w++)->decoded(std::identity{});
        out.de_h = (w++)->decoded(std::identity{});
        out.de_v = (w++)->decoded(std::identity{});
        const hal::SpiWord clocks_low = *w++;
        out.frame_clocks = hal::SpiHalves{
            .low = clocks_low,
            .high = hal::SpiWord{w->low_byte()}
        }.joined(dword);
        return out;
    }

    [[nodiscard]] constexpr VideoSample with_fb_par_run(
        std::span<const hal::SpiWord, kFbParRunWords> run
    ) const noexcept {
        VideoSample out = *this;
        auto w = run.begin();
        out.aspect.arx_raw = (w++)->decoded(std::identity{});
        out.aspect.ary_raw = (w++)->decoded(std::identity{});
        out.fb_fmt = (w++)->decoded(std::identity{});
        out.fb_width = (w++)->decoded(std::identity{});
        out.fb_height = w->decoded(std::identity{});
        return out;
    }

    [[nodiscard]] bool interlaced() const noexcept { return res.interlaced(); }

    [[nodiscard]] bool rotated() const noexcept { return res.rotated(); }

    [[nodiscard]] bool arxy() const noexcept { return aspect.arxy(); }

    [[nodiscard]] std::uint16_t arx() const noexcept { return aspect.arx(); }

    [[nodiscard]] std::uint16_t ary() const noexcept { return aspect.ary(); }

    [[nodiscard]] bool fb_en() const noexcept {
        constexpr std::uint16_t kFbEnable = 0x40;
        return (fb_fmt & kFbEnable) != 0;
    }
};

static_assert(
    sizeof(ResWord) == 2 && sizeof(AspectRaw) == 4,
    "both are fabric reply words, not widened records"
);

}
