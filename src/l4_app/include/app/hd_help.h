// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace mister::app {

struct HdHelp {
    enum class Glyph : std::uint8_t { None, North, East, South, West, DpadLr };
    enum class Lr : std::uint8_t { None, Change, Move };

    struct Item {
        Glyph glyph = Glyph::None;
        std::string_view label{};
    };

    static constexpr std::size_t kMaxItems = 4;
    std::array<Item, kMaxItems> items{};
    std::uint8_t count = 0;
};

[[nodiscard]] constexpr HdHelp::Glyph hd_help_glyph(char key) noexcept {
    switch (key) {
        case 'A':
            return HdHelp::Glyph::South;
        case 'B':
            return HdHelp::Glyph::East;
        case 'X':
            return HdHelp::Glyph::West;
        case 'Y':
            return HdHelp::Glyph::North;
        default:
            return HdHelp::Glyph::None;
    }
}

[[nodiscard]] constexpr HdHelp hd_help(std::string_view footer, HdHelp::Lr lr) noexcept {
    HdHelp out{};
    const auto push = [&out](HdHelp::Glyph g, std::string_view label) {
        if (out.count < HdHelp::kMaxItems)
            out.items[out.count++] = HdHelp::Item{g, label};
    };
    const auto trim = [](std::string_view s) {
        while (!s.empty() && s.front() == ' ')
            s.remove_prefix(1);
        while (!s.empty() && s.back() == ' ')
            s.remove_suffix(1);
        return s;
    };
    if (trim(footer).empty()) {
        push(HdHelp::Glyph::South, "Select");
        push(HdHelp::Glyph::East, "Back");
    }
    std::string_view rest = footer;
    while (!(rest = trim(rest)).empty()) {
        const std::size_t cut = rest.find("  ");
        const std::string_view item = rest.substr(0, cut);
        rest = cut == std::string_view::npos ? std::string_view{} : rest.substr(cut);
        const HdHelp::Glyph g = item.size() > 2 && item[1] == ' ' ? hd_help_glyph(item[0])
                                                                  : HdHelp::Glyph::None;
        push(g, g == HdHelp::Glyph::None ? item : trim(item.substr(2)));
    }
    if (lr == HdHelp::Lr::Change)
        push(HdHelp::Glyph::DpadLr, "Change");
    if (lr == HdHelp::Lr::Move)
        push(HdHelp::Glyph::DpadLr, "Move");
    return out;
}

}
