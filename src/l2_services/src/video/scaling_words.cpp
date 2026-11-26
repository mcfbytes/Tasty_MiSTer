// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video/scaling_words.h"
#include "hal/link_halves.h"
#include "svc/video/scaling_policy.h"
#include "svc/video/video_sample.h"

namespace mister::svc::video {

ScalingWords scaling_words(const VideoSample& s, const ScalingPolicy& pol) noexcept {

    constexpr std::uint32_t kBorderDoubling = 2;
    constexpr std::uint32_t kMinRemainder = 100;
    ScalingWords out{};
    if (pol.scales_by_resolution())
        return out;
    const Resolution pic = s.picture();
    const std::uint32_t height = pic.height;
    const std::uint32_t width = pic.width;

    const bool hold = !pol.scaler.downscale();
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
            out.height = hal::LinkHalves::of(scrh).low;
        } else if (pol.vscale_border != 0) {
            std::uint32_t border = pol.vscale_border * kBorderDoubling;
            if ((border + kMinRemainder) > scrh)
                border = scrh - kMinRemainder;
            scrh -= border;
            if (hold && scrh < height && height <= pol.scrh)
                scrh = height;
            out.height = hal::LinkHalves::of(scrh).low;
        }
    }
    if (pol.scrw != 0) {
        if (pol.vscale_border != 0 && !(pol.steps_height() && height != 0)) {
            std::uint32_t scrw = pol.scrw;
            std::uint32_t border = pol.vscale_border * kBorderDoubling;
            if ((border + kMinRemainder) > scrw)
                border = scrw - kMinRemainder;
            scrw -= border;
            if (hold && scrw < width && width <= pol.scrw)
                scrw = width;
            out.width = hal::LinkHalves::of(scrw).low;
        }
    }
    return out;
}

}
