// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_crumb.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace mister::app {
namespace {

constexpr std::string_view kSeparator = " \u203A ";

std::string ascii_upper(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char ch : s)
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    return out;
}

}  // namespace

FixedStr<48, StrFit::Clip> hd_crumb(std::string_view core_name, std::string_view title) {
    constexpr std::size_t kCapacity = FixedStr<48, StrFit::Clip>::kCapacity;
    FixedStr<48, StrFit::Clip> out;
    if (core_name.empty()) core_name = "CORE";
    std::string tail(kSeparator);
    tail += ascii_upper(title);
    std::size_t keep = 0;
    if (tail.size() < kCapacity) {
        keep = std::min(core_name.size(), kCapacity - tail.size());
        while (keep > 0 && keep < core_name.size() &&
               (static_cast<unsigned char>(core_name[keep]) & 0xC0u) == 0x80u)
            --keep;
    }
    std::string text = ascii_upper(core_name.substr(0, keep));
    text += tail;
    (void)out.assign(text);
    return out;
}

std::string_view hd_crumb_core(std::string_view crumb) noexcept {
    const std::size_t cut = crumb.find(kSeparator);
    return cut == std::string_view::npos ? crumb : crumb.substr(0, cut);
}

}  // namespace mister::app
