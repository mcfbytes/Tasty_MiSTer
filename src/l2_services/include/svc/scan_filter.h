// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::svc {

struct ScanFilter {

    std::string_view extensions;
    bool directories = true;
    bool files = true;
    bool zip_as_directory = true;
    bool romset_browse = false;
};

}  // namespace mister::svc
