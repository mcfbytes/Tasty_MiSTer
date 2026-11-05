// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_label.h"

#include <array>
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

bool take_function_key(std::string_view& v) noexcept {
    constexpr std::size_t kDecimalBase = 10;
    constexpr std::size_t kFirstFunctionKey = 1;
    constexpr std::size_t kLastFunctionKey = 12;
    if (v.empty() || v.front() != 'F')
        return false;
    v.remove_prefix(1);
    if (v.empty() || !digit(v.front()))
        return false;
    std::size_t n = 0;
    while (!v.empty() && digit(v.front())) {
        n = n * kDecimalBase + static_cast<std::size_t>(v.front() - '0');
        v.remove_prefix(1);
    }
    return n >= kFirstFunctionKey && n <= kLastFunctionKey;
}

bool is_key(std::string_view s) noexcept {
    std::string_view rest = s;
    if (take_function_key(rest)) {
        if (rest.empty())
            return true;
        if (rest.front() != '-')
            return false;
        rest.remove_prefix(1);
        return take_function_key(rest) && rest.empty();
    }
    return s.size() == 1 && alnum(s[0]);
}

constexpr std::array<std::string_view, 4> kModifiers{"Alt", "Win", "Ctrl", "Shift"};

bool is_key_chord(std::string_view s) noexcept {
    std::size_t i = 0;
    for (;;) {
        std::size_t m = 0;
        for (const std::string_view mod : kModifiers) {
            if (s.substr(i).starts_with(mod)) {
                m = mod.size();
                break;
            }
        }
        if (m == 0 || i + m >= s.size() || (s[i + m] != '-' && s[i + m] != '+'))
            break;
        i += m + 1;
    }
    return is_key(s.substr(i));
}

constexpr std::string_view kMaskLead = " *.";
constexpr std::string_view kEmptyHint = "()";

std::size_t mask_keep_len(std::string_view s) noexcept {
    const std::size_t star = s.rfind(kMaskLead);
    if (star == kNone)
        return kNone;
    const std::string_view ext = s.substr(star + kMaskLead.size());
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
    if (s.size() < kEmptyHint.size() || s.back() != ')')
        return kNone;
    const std::size_t open = s.rfind('(');
    if (open == kNone)
        return kNone;
    std::string_view inner = s.substr(open + 1);
    inner.remove_suffix(1);
    if (!is_key_chord(inner))
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
