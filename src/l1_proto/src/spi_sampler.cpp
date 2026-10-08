// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_sampler.h"

namespace mister::proto {
namespace {

hal::PinLevels decoded(const hal::SpiSample& g) noexcept {
    return hal::PinLevels{
        .core_ready = g.ready,
        .menu_button = (g.buttons & hal::SpiSample::kMenuButton) != 0u,
        .user_button = (g.buttons & hal::SpiSample::kUserButton) != 0u,
        .hdmi_int = g.hdmi,
    };
}

}  // namespace

void SpiSampler::service(bool hold_buttons) noexcept {
    hal::SpiSample g = src_->sample();
    if (hold_buttons && seen_) g.buttons = last_.buttons;
    const bool first = !seen_;
    if (!first) {
        if (g.ready != last_.ready) (void)out_.push(LinkEvent::ReadyEdge{.ready = g.ready});
        if (g.buttons != last_.buttons) {
            const hal::PinLevels lv = decoded(g);
            (void)out_.push(LinkEvent::ButtonLevel{.osd = lv.menu_button, .user = lv.user_button});
        }
    }
    if (first || g.ready != last_.ready || g.buttons != last_.buttons || g.hdmi != last_.hdmi) {
        levels_->publish(decoded(g));
    }
    last_ = g;
    seen_ = true;
}

}  // namespace mister::proto
