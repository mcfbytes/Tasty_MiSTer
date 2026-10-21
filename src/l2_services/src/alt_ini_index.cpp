// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/alt_ini_index.h"
#include "config_detail.h"

#include <algorithm>
#include <string>
#include <span>

namespace mister::svc {

namespace {

using detail::icmp_n;
using detail::iequals;

int icmp_full(std::string_view a, std::string_view b) {
    return icmp_n(a, b, std::max(a.size(), b.size()) + 1);
}

}

AltIniIndex AltIniIndex::scan(std::span<const std::string> root_dirents) {
    AltIniIndex idx;

    std::size_t found = 0;
    for (const std::string& d : root_dirents) {
        if (found >= kMaxAltInis)
            break;

        if (icmp_n(d, "MiSTer_", 7) != 0)
            continue;
        if (!iequals(std::string_view{d}.substr(d.size() - 4), ".ini"))
            continue;
        idx.names_[found] = d;

        if (idx.names_[found].size() > 63)
            idx.names_[found].resize(63);
        ++found;
    }

    for (std::size_t i = 1; i < kMaxAltInis; ++i) {
        for (std::size_t j = 1; j < kMaxAltInis; ++j) {
            const bool prev_empty = idx.names_[j - 1].empty();
            const bool cur_empty = idx.names_[j].empty();
            const bool swap_them = (prev_empty && !cur_empty)
                || (!prev_empty && !cur_empty && icmp_full(idx.names_[j - 1], idx.names_[j]) > 0);
            if (swap_them)
                idx.names_[j - 1].swap(idx.names_[j]);
        }
    }
    return idx;
}

std::string_view AltIniIndex::name_for(std::uint8_t alt) const {
    if (alt == 0 || alt >= 4)
        return "MiSTer.ini";
    return names_[alt - 1];
}

std::string AltIniIndex::label_for(std::uint8_t alt) const {

    if (alt == 0)
        return "Main";
    const std::string_view name = name_for(alt);
    if (name.empty())
        return " -- ";

    char label[6] = {};
    const std::string_view tail = name.substr(7);
    const std::size_t n = std::min<std::size_t>(tail.size(), 5);
    for (std::size_t k = 0; k < n; ++k)
        label[k] = tail[k];

    for (std::size_t k = n; k-- > 0;) {
        if (label[k] == '.') {
            label[k] = '\0';
            break;
        }
    }

    const std::string_view stem{label};
    if (iequals(stem, "alt"))
        return "Alt1";
    if (iequals(stem, "alt_1"))
        return "Alt1";
    if (iequals(stem, "alt_2"))
        return "Alt2";
    if (iequals(stem, "alt_3"))
        return "Alt3";

    for (std::size_t k = 0; k < 4; ++k) {
        if (label[k] == '\0')
            label[k] = ' ';
    }
    label[4] = '\0';
    return std::string{label};
}

}
