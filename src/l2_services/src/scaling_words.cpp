// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/scaling_words.h"
#include "svc/scaling_policy.h"
#include "svc/video_sample.h"

namespace mister::svc {

ScalingWords scaling_words(const VideoSample& s, const ScalingPolicy& pol) noexcept {
    ScalingWords out{};
    if (pol.vscale_mode >= 4)
        return out;
    const std::uint32_t height = s.rotated() ? s.width : s.height;
    if (pol.scrh != 0) {
        std::uint32_t scrh = pol.scrh;
        if (pol.vscale_mode != 0 && height != 0) {
            const std::uint32_t div = 1u << (pol.vscale_mode - 1u);
            const std::uint32_t mag = (scrh * div) / height;
            scrh = (height * mag) / div;
            out.height = static_cast<std::uint16_t>(scrh);
        } else if (pol.vscale_border != 0) {
            std::uint32_t border = pol.vscale_border * 2u;
            if ((border + 100u) > scrh)
                border = scrh - 100u;
            scrh -= border;
            out.height = static_cast<std::uint16_t>(scrh);
        }
    }
    if (pol.scrw != 0) {
        if (pol.vscale_border != 0 && !(pol.vscale_mode != 0 && height != 0)) {
            std::uint32_t scrw = pol.scrw;
            std::uint32_t border = pol.vscale_border * 2u;
            if ((border + 100u) > scrw)
                border = scrw - 100u;
            scrw -= border;
            out.width = static_cast<std::uint16_t>(scrw);
        }
    }
    return out;
}

}
