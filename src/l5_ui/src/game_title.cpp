// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/game_title.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace mister::ui {

namespace {

constexpr auto kPlaces = std::to_array<std::string_view>({
    "USA",         "Europe",
    "Japan",       "World",
    "Asia",        "Korea",
    "China",       "Taiwan",
    "Hong Kong",   "Brazil",
    "Australia",   "Canada",
    "France",      "Germany",
    "Spain",       "Italy",
    "Netherlands", "Sweden",
    "Scandinavia", "UK",
    "Russia",      "Mexico",
    "Argentina",   "India",
    "Greece",      "Poland",
    "Portugal",    "Denmark",
    "Norway",      "Finland",
    "Belgium",     "Switzerland",
    "Austria",     "Latin America",
    "U",           "E",
    "J",           "W",
    "UE",          "EU",
    "JU",          "UJ",
    "JUE",         "JE",
    "EJ",          "UEJ",
    "K",           "HK",
    "En",          "Fr",
    "De",          "Es",
    "It",          "Nl",
    "Pt",          "Sv",
    "No",          "Da",
    "Fi",          "Ja",
    "Zh",          "Ko",
    "Pl",          "Ru",
    "Ca",          "Cs",
    "Hu",          "El",
    "Tr",          "Ar",
    "He",
});

bool same_letters(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x))
                   == std::tolower(static_cast<unsigned char>(y));
           });
}

std::string_view trimmed(std::string_view s) noexcept {
    while (!s.empty() && s.front() == ' ')
        s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ')
        s.remove_suffix(1);
    return s;
}

bool names_places(std::string_view inside) noexcept {
    if (trimmed(inside).empty())
        return false;
    while (!inside.empty()) {
        const std::size_t cut = inside.find_first_of(",+");
        const std::string_view item = trimmed(inside.substr(0, cut));
        if (std::ranges::none_of(kPlaces, [item](std::string_view p) {
                return same_letters(p, item);
            }))
            return false;
        if (cut == std::string_view::npos)
            break;
        inside.remove_prefix(cut + 1);
    }
    return true;
}

std::string_view stem_of(std::string_view path) noexcept {
    if (const std::size_t slash = path.rfind('/'); slash != std::string_view::npos)
        path.remove_prefix(slash + 1);
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos || dot == 0)
        return path;
    const std::string_view ext = path.substr(dot + 1);
    const bool short_ext = !ext.empty() && ext.size() <= 4 && std::ranges::all_of(ext, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0;
    });
    return short_ext ? path.substr(0, dot) : path;
}

}

std::string game_title(std::string_view path) {
    const std::string_view stem = stem_of(path);
    std::string out;
    for (std::size_t i = 0; i < stem.size();) {
        const char open = stem[i];
        const char close = open == '(' ? ')' : open == '[' ? ']' : '\0';
        const std::size_t end = close != '\0' ? stem.find(close, i + 1) : std::string_view::npos;
        if (end == std::string_view::npos) {
            out.push_back(open);
            ++i;
            continue;
        }
        const std::string_view inside = stem.substr(i + 1, end - i - 1);
        if (open == '(' && !names_places(inside))
            out.append(stem.substr(i, end - i + 1));
        i = end + 1;
    }
    std::string title;
    for (const char c : out)
        if (c != ' ' || (!title.empty() && title.back() != ' '))
            title.push_back(c);
    while (!title.empty() && title.back() == ' ')
        title.pop_back();
    return title.empty() ? std::string(stem) : title;
}

}
