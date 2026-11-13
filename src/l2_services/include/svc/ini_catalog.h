// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "svc/alt_ini_index.h"

namespace mister::svc {

struct IniCatalog {
    AltIniIndex index{};
    IniChoice active = IniChoice::Main;

    [[nodiscard]] std::string_view active_name() const noexcept { return index.name_for(active); }

    [[nodiscard]] bool has(IniChoice c) const noexcept { return index.has(c); }
};

}
