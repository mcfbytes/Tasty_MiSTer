// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/rom_bytes.h"

#include <algorithm>

namespace mister::cores::mra {

bool RomBytes::resize(RomOffset end) noexcept {
    const RomOffset mapped = RomOffset{} + RomLength::of(mapped_());
    if (end > mapped && !map_.resize((end - RomOffset{}).size()))
        return false;

    const RomOffset stale_end = std::min(end, mapped);
    if (stale_end > end_)
        std::ranges::fill((stale_end - end_).first_of(end_.onward(mapped_())), 0);
    end_ = end;
    return true;
}

}
