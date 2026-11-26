// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/spi_transport.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/link/fio_bracket_words.h"

namespace mister::proto::link::detail {

inline constexpr hal::LinkWord kModeDownload = FioBracketWords::kModeDownload;
inline constexpr hal::LinkWord kModeUpload = FioBracketWords::kModeUpload;
inline constexpr hal::LinkWord kModeEnd = FioBracketWords::kModeEnd;

[[nodiscard]] inline Ex<hal::LinkWord> beat(hal::ISpiTransport& link, hal::LinkWord out) {
    return link.transfer(out);
}

}
