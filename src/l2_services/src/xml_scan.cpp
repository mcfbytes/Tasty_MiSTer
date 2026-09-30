// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/xml_scan.h"

namespace mister::svc::xml {

bool iequal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

namespace {

bool skip_markup(std::string_view xml, std::size_t& pos) noexcept {
    if (xml.compare(pos, 3, "!--") == 0) {
        const std::size_t end = xml.find("-->", pos + 3);
        if (end == std::string_view::npos) return false;
        pos = end + 3;
        return true;
    }
    const std::size_t end = xml.find('>', pos);
    if (end == std::string_view::npos) return false;
    pos = end + 1;
    return true;
}

}  // namespace

bool next_token(std::string_view xml, std::size_t& pos, Token& t, Attr* attrs,
                std::size_t& attr_count) noexcept {
    attr_count = 0;
    for (;;) {
        const std::size_t lt = xml.find('<', pos);
        if (lt == std::string_view::npos) {

            return false;
        }

        if (lt > pos) {
            t.kind = Token::Kind::Text;
            t.text = xml.substr(pos, lt - pos);
            pos = lt;
            return true;
        }
        pos = lt + 1;
        if (pos >= xml.size()) return false;

        if (xml[pos] == '!' || xml[pos] == '?') {
            if (!skip_markup(xml, pos)) return false;
            t.kind = Token::Kind::Markup;
            t.tag = Tag{};
            return true;
        }

        Tag tag{};
        tag.closing = (xml[pos] == '/');
        if (tag.closing) ++pos;

        const std::size_t name_start = pos;
        while (pos < xml.size() && !is_space(xml[pos]) && xml[pos] != '>' && xml[pos] != '/') {
            ++pos;
        }
        tag.name = xml.substr(name_start, pos - name_start);
        if (tag.name.empty()) return false;

        for (;;) {
            while (pos < xml.size() && is_space(xml[pos]))
                ++pos;
            if (pos >= xml.size()) return false;
            if (xml[pos] == '>') {
                ++pos;
                break;
            }
            if (xml[pos] == '/') {
                ++pos;
                if (pos < xml.size() && xml[pos] == '>') ++pos;
                tag.self_closing = true;
                break;
            }
            const std::size_t an = pos;
            while (pos < xml.size() && !is_space(xml[pos]) && xml[pos] != '=' && xml[pos] != '>' &&
                   xml[pos] != '/') {
                ++pos;
            }
            const std::string_view name = xml.substr(an, pos - an);
            while (pos < xml.size() && is_space(xml[pos]))
                ++pos;
            if (pos >= xml.size() || xml[pos] != '=') continue;
            ++pos;
            while (pos < xml.size() && is_space(xml[pos]))
                ++pos;
            if (pos >= xml.size()) return false;
            const char quote = xml[pos];
            if (quote != '"' && quote != '\'') continue;
            ++pos;
            const std::size_t vs = pos;
            while (pos < xml.size() && xml[pos] != quote)
                ++pos;
            if (pos >= xml.size()) return false;
            const std::string_view value = xml.substr(vs, pos - vs);
            ++pos;
            if (name.empty()) continue;
            if (attr_count < kMaxAttrs) {
                attrs[attr_count++] = Attr{name, value};
            } else if (tag.attrs_dropped != 0xFF) {
                ++tag.attrs_dropped;
            }
        }
        t.kind = tag.closing ? Token::Kind::End : Token::Kind::Start;
        t.tag = tag;
        return true;
    }
}

bool next_tag(std::string_view xml, std::size_t& pos, Tag& tag, Attr* attrs,
              std::size_t& attr_count) noexcept {
    Token t{};
    while (next_token(xml, pos, t, attrs, attr_count)) {
        if (t.kind == Token::Kind::Text || t.kind == Token::Kind::Markup) continue;
        tag = t.tag;
        return true;
    }
    return false;
}

std::string_view rbf_text(std::string_view xml) noexcept {
    std::string_view out;
    bool inside = false;
    std::size_t pos = 0;
    Token t{};
    Attr attrs[kMaxAttrs];
    std::size_t nattrs = 0;
    while (next_token(xml, pos, t, attrs, nattrs)) {
        switch (t.kind) {
            case Token::Kind::Text:

                if (inside) {
                    inside = false;
                    out = t.text;
                }
                break;
            case Token::Kind::Start:

                if (iequal(t.tag.name, "rbf")) inside = true;
                if (t.tag.self_closing) inside = false;
                break;
            case Token::Kind::End:
            case Token::Kind::Markup:

                inside = false;
                break;
        }
    }

    while (!out.empty() && is_space(out.front()))
        out.remove_prefix(1);
    while (!out.empty() && is_space(out.back()))
        out.remove_suffix(1);
    return out;
}

}  // namespace mister::svc::xml
