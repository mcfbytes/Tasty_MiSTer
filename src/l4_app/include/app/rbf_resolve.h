// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "infra/error.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

[[nodiscard]] Ex<std::string> resolve_rbf_name(const svc::Vfs& vfs, std::string_view dir,
                                               std::string_view stem, bool arcade);

}
