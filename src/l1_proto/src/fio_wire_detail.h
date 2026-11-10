// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/spi_transport.h"
#include "infra/error.h"

namespace mister::proto::detail {

inline constexpr hal::SpiWord kModeDownload{0x00FF};
inline constexpr hal::SpiWord kModeUpload{0x00AA};
inline constexpr hal::SpiWord kModeEnd{0x0000};

[[nodiscard]] inline Ex<hal::SpiWord> beat(hal::ISpiTransport& link, hal::SpiWord out) {
    return link.transfer(out);
}

}
