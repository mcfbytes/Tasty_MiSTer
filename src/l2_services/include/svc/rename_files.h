// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::svc {

struct RenameFiles {
    std::string_view tmp;
    std::string_view dest;
};

}
