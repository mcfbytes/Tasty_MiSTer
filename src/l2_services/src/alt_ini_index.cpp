// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/alt_ini_index.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "infra/ascii.h"

namespace mister::svc {

namespace {

inline constexpr std::string_view kAltStem = "MiSTer_";
inline constexpr std::string_view kAltSuffix = ".ini";
inline constexpr std::string_view kMainIni = "MiSTer.ini";
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

        (void)idx.names_[found].assign(d);
        ++found;
    }

    for (std::size_t i = 1; i < kMaxAltInis; ++i) {
        for (std::size_t j = 1; j < kMaxAltInis; ++j) {
            const bool prev_empty = idx.names_[j - 1].empty();
            const bool cur_empty = idx.names_[j].empty();
            const bool swap_them = (prev_empty && !cur_empty)
                || (!prev_empty
                    && !cur_empty
                    && infra::icompare(idx.names_[j - 1].view(), idx.names_[j].view()) > 0);
            if (swap_them)
                std::swap(idx.names_[j - 1], idx.names_[j]);
        }
    }
    return idx;
}

AltIniIndex AltIniIndex::from_scan(std::span<const DirEntry> entries) {
    std::vector<std::string> names;
    names.reserve(entries.size());
    bool main = false;
    for (const DirEntry& e : entries) {
        if (e.is_dir)
            continue;
        names.push_back(e.name);
        if (infra::ieq(e.name, kMainIni))
            main = true;
    }
    AltIniIndex idx = scan(names);
    idx.main_ = main;
    return idx;
}

bool AltIniIndex::has(IniChoice c) const noexcept {
    if (c == IniChoice::Main)
        return main_;
    const std::optional<AltIniSlot> slot = AltIniSlot::of(c);
    return slot && !names_[slot->v].empty();
}

std::string_view AltIniIndex::name_for(IniChoice c) const noexcept {
    if (c == IniChoice::Main)
        return kMainIni;
    const std::optional<AltIniSlot> slot = AltIniSlot::of(c);
    if (!slot)
        return {};
    return names_[slot->v].view();
}

std::string AltIniIndex::label_for(IniChoice c) const {

    if (c == IniChoice::Main)
        return "Main";
    const std::string_view name = name_for(c);
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
