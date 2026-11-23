// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/prelude.h"

namespace mister::svc {

class Vfs;

struct ConfineRoot {
    std::string_view path;

    [[nodiscard]] Ex<std::string> confine(const Vfs& vfs, std::string_view guest) const;
};

}
