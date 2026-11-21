// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cerrno>

#include "infra/error.h"
#include "infra/prelude.h"

namespace mister::svc::input {

[[nodiscard]] inline Error os_error(infra::ErrorSite site) noexcept {
    return infra::errno_error(Errc::os, site, errno);
}

}
