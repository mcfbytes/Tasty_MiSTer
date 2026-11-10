// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"

namespace mister::svc {

inline std::unexpected<Error> err(Errc c, infra::ErrorSite site, std::uint32_t detail = 0) {
    return std::unexpected(Error{c, site, detail});
}

}
