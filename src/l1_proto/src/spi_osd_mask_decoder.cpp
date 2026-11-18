// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_osd_mask_decoder.h"

#include "hal/selected.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

std::optional<OsdMask> SpiOsdMaskDecoder::service() noexcept {
    if (!active()) {
        div_ = 0;
        return std::nullopt;
    }
    if (div_ != 0) {
        --div_;
        return std::nullopt;
    }
    div_ = kPollTicks - 1;

    OsdMask m{};
    {
        hal::Selected cs(link_, hal::ChipSelect::Io);
        if (auto r = link_.transfer(word(UioOp::GetOsdMask)); !r) {
            ++errors_;
            return std::nullopt;
        }
        auto w = link_.transfer(hal::SpiWord{0});
        if (!w) {
            ++errors_;
            return std::nullopt;
        }
        m = OsdMask::of(*w);
    }
    if (seen_ && m == mask_) {
        rearmed_ = false;
        return std::nullopt;
    }

    if (!out_.push(LinkEvent::OsdMask{.mask = m, .front_end = front_end_}))
        return std::nullopt;
    mask_ = m;
    seen_ = true;
    rearmed_ = false;
    ++publishes_;
    return m;
}

}
