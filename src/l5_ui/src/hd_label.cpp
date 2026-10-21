// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_label.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace mister::ui {
namespace {

constexpr std::size_t kNone = std::string_view::npos;

bool alnum(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

bool is_key(std::string_view s) noexcept {
    if (s.size() >= 2 && s[0] == 'F' && digit(s[1])) {
        std::size_t n = 0;
        std::size_t i = 1;
        while (i < s.size() && digit(s[i])) {
            n = n * 10 + static_cast<std::size_t>(s[i] - '0');
            ++i;
        }
        if (n >= 1 && n <= 12) {
            const std::string_view rest = s.substr(i);
            if (rest.empty())
                return true;
            if (rest.size() >= 3 && rest[0] == '-' && rest[1] == 'F' && digit(rest[2])) {
                std::size_t m = 0;
                std::size_t k = 2;
                while (k < rest.size() && digit(rest[k])) {
                    m = m * 10 + static_cast<std::size_t>(rest[k] - '0');
                    ++k;
                }
                return k == rest.size() && m >= 1 && m <= 12;
            }
            return false;
        }
    }
    return s.size() == 1 && alnum(s[0]);
}

bool is_key_chord(std::string_view s) noexcept {
    std::size_t i = 0;
    for (;;) {
        std::size_t m = 0;
        if (s.compare(i, 3, "Alt") == 0 || s.compare(i, 3, "Win") == 0)
            m = 3;
        else if (s.compare(i, 4, "Ctrl") == 0)
            m = 4;
        else if (s.compare(i, 5, "Shift") == 0)
            m = 5;
        if (m == 0 || i + m >= s.size() || (s[i + m] != '-' && s[i + m] != '+'))
            break;
        i += m + 1;
    }
    return is_key(s.substr(i));
}

std::size_t mask_keep_len(std::string_view s) noexcept {
    const std::size_t star = s.rfind(" *.");
    if (star == kNone)
        return kNone;
    const std::string_view ext = s.substr(star + 3);
    if (ext.empty())
        return kNone;
    std::size_t i = 0;
    while (i < ext.size()) {
        std::size_t j = i;
        while (j < ext.size() && alnum(ext[j]))
            ++j;
        if (j == i)
            return kNone;
        i = j;
        if (i == ext.size())
            return star;
        if (ext[i] != ',')
            return kNone;
        ++i;
    }
    return kNone;
}

std::size_t hint_keep_len(std::string_view s) noexcept {
    if (s.size() < 2 || s.back() != ')')
        return kNone;
    const std::size_t open = s.rfind('(');
    if (open == kNone)
        return kNone;
    if (!is_key_chord(s.substr(open + 1, s.size() - open - 2)))
        return kNone;
    return open;
}

}

std::string hd_label(std::string_view label) {
    std::string_view s = label;
    for (;;) {
        std::size_t keep = mask_keep_len(s);
        if (keep == kNone)
            keep = hint_keep_len(s);
        if (keep == kNone)
            break;
        s = s.substr(0, keep);
        std::size_t end = s.size();
        while (end > 0 && s[end - 1] == ' ')
            --end;
        s = s.substr(0, end);
    }
    if (s.empty() && !label.empty())
        return std::string{label};
    return std::string{s};
}

}
