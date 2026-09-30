// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_osd_mask_decoder.h"

#include "hal/selected.h"

namespace mister::proto {

namespace {
constexpr std::uint16_t kUioGetOsdMask = 0x2E;
}

void SpiOsdMaskDecoder::service() noexcept {
    if (!active()) {
        div_ = 0;
        return;
    }
    if (div_ != 0) {
        --div_;
        return;
    }
    div_ = kPollTicks - 1;

    OsdMask m{};
    {
        hal::Selected cs(*link_, hal::ChipSelect::Io);
        if (auto r = link_->transfer(hal::SpiWord{kUioGetOsdMask}); !r) {
            ++errors_;
            return;
        }
        auto w = link_->transfer(hal::SpiWord{0});
        if (!w) {
            ++errors_;
            return;
        }
        m = OsdMask{static_cast<std::uint16_t>(w->v)};
    }
    if (seen_ && m == mask_) return;

    if (!out_.push(LinkEvent::OsdMask{.mask = m, .front_end = front_end_})) return;
    mask_ = m;
    seen_ = true;
    ++publishes_;
}

}  // namespace mister::proto
