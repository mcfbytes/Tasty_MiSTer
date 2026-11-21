// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video/scaling_words.h"
#include "hal/spi_halves.h"
#include "svc/video/scaling_policy.h"
#include "svc/video/video_sample.h"

namespace mister::svc::video {

ScalingWords scaling_words(const VideoSample& s, const ScalingPolicy& pol) noexcept {

    constexpr std::uint32_t kBorderDoubling = 2;
    constexpr std::uint32_t kMinRemainder = 100;
    ScalingWords out{};
    if (pol.scales_by_resolution())
        return out;
    const std::uint32_t height = s.rotated() ? s.active.width : s.active.height;
    if (pol.scrh != 0) {
        std::uint32_t scrh = pol.scrh;
        if (pol.steps_height() && height != 0) {

            std::uint32_t div = 1;
            if (pol.vscale_mode == VscaleMode::HalfStep)
                div = 2;
            if (pol.vscale_mode == VscaleMode::QuarterStep)
                div = 4;
            const std::uint32_t mag = (scrh * div) / height;
            scrh = (height * mag) / div;
            out.height = hal::SpiHalves::of(scrh).low;
        } else if (pol.vscale_border != 0) {
            std::uint32_t border = pol.vscale_border * kBorderDoubling;
            if ((border + kMinRemainder) > scrh)
                border = scrh - kMinRemainder;
            scrh -= border;
            out.height = hal::SpiHalves::of(scrh).low;
        }
    }
    if (pol.scrw != 0) {
        if (pol.vscale_border != 0 && !(pol.steps_height() && height != 0)) {
            std::uint32_t scrw = pol.scrw;
            std::uint32_t border = pol.vscale_border * kBorderDoubling;
            if ((border + kMinRemainder) > scrw)
                border = scrw - kMinRemainder;
            scrw -= border;
            out.width = hal::SpiHalves::of(scrw).low;
        }
    }
    return out;
}

}
