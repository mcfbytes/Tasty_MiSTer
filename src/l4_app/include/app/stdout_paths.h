// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

struct StdoutPaths {
    const char* null_sink = "/dev/null";
    const char* debug_file = "/tmp/debug.txt";
};

}
