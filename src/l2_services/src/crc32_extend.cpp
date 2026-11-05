// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <span>

#include "infra/crc32.h"
#include "svc/types.h"

namespace mister::svc {

Crc32 crc32_extend(Crc32 running, std::span<const std::uint8_t> data) noexcept {
    return Crc32{crc32_update(running.v, data)};
}

}
