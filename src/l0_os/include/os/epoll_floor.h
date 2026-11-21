// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/prelude.h"

namespace mister::os {

[[nodiscard]] Ex<void> check_epoll_pwait2() noexcept;

}
