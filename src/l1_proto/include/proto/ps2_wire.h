// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "proto/link_frame.h"
#include "proto/ps2_frame.h"

namespace mister::proto {

[[nodiscard]] Ex<void> write_ps2_frame(ILinkFrame& frame, const Ps2Frame& f);

}
