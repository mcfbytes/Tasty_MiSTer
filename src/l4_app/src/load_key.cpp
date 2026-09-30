// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/load_key.h"

#include "infra/crc32.h"

namespace mister::app {

LoadKey::LoadKey(std::uint64_t total) noexcept : skip_(crc32_header_skip(total)) {}

void LoadKey::feed(std::span<const std::uint8_t> bytes) noexcept {
    if (skip_ >= bytes.size()) {
        skip_ -= bytes.size();
        return;
    }
    crc_ = crc32_update(crc_, bytes.subspan(static_cast<std::size_t>(skip_)));
    skip_ = 0;
}

}  // namespace mister::app
