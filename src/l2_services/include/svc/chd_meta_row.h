// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::svc {

struct ChdMetaRow {
    std::string_view text;
    bool v2 = true;
};

}
