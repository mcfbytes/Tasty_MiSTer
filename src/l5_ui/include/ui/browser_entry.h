// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace mister::ui {

struct BrowserEntry {
    std::string name;
    std::string altname;
    std::string datecode;

    std::string dir;
    bool is_dir = false;
    bool from_zip = false;
    bool stippled = false;

    bool recent = false;
};

}
