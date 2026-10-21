// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cerrno>
#include <cstdint>

#include "infra/error.h"

namespace mister::svc {

namespace {

Error os_error(std::uint16_t site) {
    return Error{Errc::os, site, static_cast<std::uint32_t>(errno)};
}

}

}
