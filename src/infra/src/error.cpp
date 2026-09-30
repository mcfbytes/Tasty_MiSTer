// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/error.h"

#include <cstdio>
#include <cstdlib>

namespace mister {

[[noreturn]] void fatal(Error e, const char* what) {
    std::fprintf(stderr, "FATAL: %s (code=%u site=%u detail=%u)\n", what,
                 static_cast<unsigned>(e.code), e.site, e.detail);
    std::abort();
}

}  // namespace mister
