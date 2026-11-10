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
    const std::span<const char> blob = raw();
    std::size_t i = 0;
    while (i < blob.size()) {
        const std::string_view rest{blob.data() + i, blob.size() - i};
        const std::string_view entry = rest.substr(0, rest.find('\0'));
        if (entry == s)
            return true;
        i += entry.size() + 1;
    }
    return false;
}

}
