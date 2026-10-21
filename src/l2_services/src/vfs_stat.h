// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/stat.h>

#include <string>

namespace mister::svc::detail {

inline bool path_exists(const std::string& p) {
    struct ::stat st{};
    return ::stat(p.c_str(), &st) == 0;
}

inline bool path_is_dir(const std::string& p) {
    struct ::stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

inline bool path_is_regular(const std::string& p) {
    struct ::stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

}
