// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "hal/spi_transport.h"
#include "proto/ps2_frame.h"

namespace mister::proto {

[[nodiscard]] Ex<void> write_ps2_frame(hal::ISpiTransport& link, const Ps2Frame& f);

}
