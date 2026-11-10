// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/ascii.h"
#include "infra/error.h"

namespace mister::os {

[[nodiscard]] Ex<infra::ByteCount> read_vm_rss_bytes() noexcept;

}
