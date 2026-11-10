// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/alt_ini_index.h"

#include <algorithm>
#include <string>
#include <span>

#include "infra/ascii.h"
#include "infra/indexed_array.h"

namespace mister::svc {

namespace {

inline constexpr std::string_view kAltStem = "MiSTer_";
inline constexpr std::string_view kAltSuffix = ".ini";
inline constexpr std::size_t kAltPathBytes = 63;
inline constexpr std::size_t kAltLabelBytes = 5;
inline constexpr std::size_t kAltLabelPad = 4;

}

AltIniIndex AltIniIndex::scan(std::span<const std::string> root_dirents) {
    AltIniIndex idx;

    std::size_t found = 0;
    for (const std::string& d : root_dirents) {
        if (found >= kMaxAltInis)
            break;

        if (infra::icompare_n(d, kAltStem, infra::ByteCount{kAltStem.size()}) != 0)
            continue;
        if (!infra::ieq(std::string_view{d}.substr(d.size() - kAltSuffix.size()), kAltSuffix)) {
            continue;
        }
        idx.names_[found] = d;

        if (idx.names_[found].size() > kAltPathBytes)
            idx.names_[found].resize(kAltPathBytes);
        ++found;
    }

    for (std::size_t i = 1; i < kMaxAltInis; ++i) {
        for (std::size_t j = 1; j < kMaxAltInis; ++j) {
            const bool prev_empty = idx.names_[j - 1].empty();
            const bool cur_empty = idx.names_[j].empty();
            const bool swap_them = (prev_empty && !cur_empty)
                || (!prev_empty
                    && !cur_empty
                    && infra::icompare(idx.names_[j - 1], idx.names_[j]) > 0);
            if (swap_them)
                idx.names_[j - 1].swap(idx.names_[j]);
        }
    }
    return idx;
}

std::string_view AltIniIndex::name_for(std::uint8_t alt) const {
    const std::optional<AltIniSlot> slot = AltIniSlot::of(alt);
    if (!slot)
        return "MiSTer.ini";
    return infra::at_index(names_, *slot);
}

std::string AltIniIndex::label_for(std::uint8_t alt) const {

    if (alt == 0)
        return "Main";
    const std::string_view name = name_for(alt);
    if (name.empty())
        return " -- ";

    char label[kAltLabelBytes + 1] = {};
    const std::string_view tail = name.substr(kAltStem.size());
    const std::size_t n = std::min<std::size_t>(tail.size(), kAltLabelBytes);
    for (std::size_t k = 0; k < n; ++k)
        label[k] = tail[k];

    for (std::size_t k = n; k-- > 0;) {
        if (label[k] == '.') {
            label[k] = '\0';
            break;
        }
    }

    const std::string_view stem{label};
    if (infra::ieq(stem, "alt"))
        return "Alt1";
    if (infra::ieq(stem, "alt_1"))
        return "Alt1";
    if (infra::ieq(stem, "alt_2"))
        return "Alt2";
    if (infra::ieq(stem, "alt_3"))
        return "Alt3";

    for (std::size_t k = 0; k < kAltLabelPad; ++k) {
        if (label[k] == '\0')
            label[k] = ' ';
    }
    label[kAltLabelPad] = '\0';
    return std::string{label};
}

}
