// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/compatible_blob.h"

#include <cstring>

namespace mister::hal {

Ex<void> CompatibleBlob::assign(std::span<const char> raw) noexcept {
    if (raw.size() > kCap) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    std::memcpy(data_.data(), raw.data(), raw.size());
    len_ = raw.size();
    return {};
}

bool CompatibleBlob::contains(std::string_view s) const noexcept {
    std::size_t i = 0;
    while (i < len_) {
        const std::string_view entry{data_.data() + i};
        if (entry == s) return true;
        i += entry.size() + 1;
    }
    return false;
}

}  // namespace mister::hal
