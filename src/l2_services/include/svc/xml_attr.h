// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::svc::xml {

struct Attr {
    std::string_view name;
    std::string_view value;
};

}
