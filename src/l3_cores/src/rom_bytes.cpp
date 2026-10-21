// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/rom_bytes.h"

#include <algorithm>
#include <cstring>

namespace mister::cores::mra {

bool RomBytes::resize(std::size_t bytes) noexcept {
    const std::size_t mapped = map_.size();
    if (bytes > mapped && !map_.resize(bytes))
        return false;

    const std::size_t stale_end = std::min(bytes, mapped);
    if (stale_end > size_)
        std::memset(map_.data() + size_, 0, stale_end - size_);
    size_ = bytes;
    return true;
}

}
