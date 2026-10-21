// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ini_key_edit.h"

#include <cstddef>
#include <optional>

namespace mister::app {

namespace {

bool is_blank(char c) noexcept {
    return c == ' ' || c == '\t';
}

char lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
}

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i]))
            return false;
    }
    return true;
}

std::string_view skip_blanks(std::string_view s) noexcept {
    while (!s.empty() && is_blank(s.front()))
        s.remove_prefix(1);
    return s;
}

std::optional<std::string_view> header_name(std::string_view line) noexcept {
    line = skip_blanks(line);
    if (line.empty() || line.front() != '[')
        return std::nullopt;
    line.remove_prefix(1);
    return line.substr(0, line.find(']'));
}

struct ValueSpan {
    std::size_t at;
    std::size_t end;
};

std::optional<ValueSpan> value_span(std::string_view line, std::string_view key) noexcept {
    std::size_t i = 0;
    while (i < line.size() && is_blank(line[i]))
        ++i;
    if (line.size() - i <= key.size() || !iequals(line.substr(i, key.size()), key))
        return std::nullopt;
    i += key.size();
    if (line[i] != '=' && !is_blank(line[i]))
        return std::nullopt;
    while (i < line.size() && (line[i] == '=' || is_blank(line[i])))
        ++i;
    const std::size_t at = i;
    while (i < line.size() && !is_blank(line[i]) && line[i] != ';')
        ++i;
    return ValueSpan{at, i};
}

}

std::string ini_set_key(std::string_view text, std::string_view key, std::string_view value) {
    const std::size_t first_nl = text.find('\n');
    const std::string_view eol = first_nl != std::string_view::npos
            && first_nl > 0
            && text[first_nl - 1] == '\r'
        ? std::string_view{"\r\n"}
        : std::string_view{"\n"};

    std::string out;
    out.reserve(text.size() + key.size() + value.size() + 16);
    bool in_section = false;
    bool in_main = false;
    bool main_has_key = false;
    std::size_t insert_at = std::string::npos;
    std::string_view header_eol = eol;

    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::size_t next = nl == std::string_view::npos ? text.size() : nl + 1;
        std::string_view line = text.substr(
            pos,
            (nl == std::string_view::npos ? text.size() : nl) - pos
        );
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        std::string_view bare = line;
        if (pos == 0 && bare.starts_with("\xEF\xBB\xBF"))
            bare.remove_prefix(3);

        const std::optional<std::string_view> name = header_name(bare);
        const std::optional<ValueSpan> span = name || !in_section ? std::nullopt
                                                                  : value_span(line, key);
        if (name) {
            in_section = true;
            in_main = iequals(*name, "MiSTer");
            out.append(text.substr(pos, next - pos));
            if (in_main && insert_at == std::string::npos) {
                if (nl == std::string_view::npos) {
                    out.append(eol);
                } else {
                    header_eol = text.substr(pos + line.size(), next - pos - line.size());
                }
                insert_at = out.size();
            }
        } else if (span) {
            if (in_main)
                main_has_key = true;
            out.append(text.substr(pos, span->at));
            out.append(value);
            out.append(text.substr(pos + span->end, next - pos - span->end));
        } else {
            out.append(text.substr(pos, next - pos));
        }
        pos = next;
    }

    if (main_has_key)
        return out;
    std::string row{key};
    row += '=';
    row += value;
    if (insert_at != std::string::npos) {
        row += header_eol;
        out.insert(insert_at, row);
        return out;
    }
    if (!out.empty() && out.back() != '\n')
        out.append(eol);
    out += "[MiSTer]";
    out.append(eol);
    out += row;
    out.append(eol);
    return out;
}

}
