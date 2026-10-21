// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/xml_scanner.h"

namespace mister::svc::xml {

bool XmlScanner::skip_markup() noexcept {
    if (xml_.compare(pos_, 3, "!--") == 0) {
        const std::size_t end = xml_.find("-->", pos_ + 3);
        if (end == std::string_view::npos)
            return false;
        pos_ = end + 3;
        return true;
    }
    const std::size_t end = xml_.find('>', pos_);
    if (end == std::string_view::npos)
        return false;
    pos_ = end + 1;
    return true;
}

std::optional<Token> XmlScanner::next_token(std::span<Attr, kMaxAttrs> attrs) noexcept {
    for (;;) {
        const std::size_t lt = xml_.find('<', pos_);
        if (lt == std::string_view::npos) {

            return std::nullopt;
        }

        if (lt > pos_) {
            Token t;
            t.kind = Token::Kind::Text;
            t.text = xml_.substr(pos_, lt - pos_);
            pos_ = lt;
            return t;
        }
        pos_ = lt + 1;
        if (pos_ >= xml_.size())
            return std::nullopt;

        if (xml_[pos_] == '!' || xml_[pos_] == '?') {
            if (!skip_markup())
                return std::nullopt;
            Token t;
            t.kind = Token::Kind::Markup;
            t.tag = Tag{};
            return t;
        }

        Tag tag{};
        tag.closing = (xml_[pos_] == '/');
        if (tag.closing)
            ++pos_;

        const std::size_t name_start = pos_;
        while (
            pos_ < xml_.size() && !is_space(xml_[pos_]) && xml_[pos_] != '>' && xml_[pos_] != '/') {
            ++pos_;
        }
        tag.name = xml_.substr(name_start, pos_ - name_start);
        if (tag.name.empty())
            return std::nullopt;

        for (;;) {
            while (pos_ < xml_.size() && is_space(xml_[pos_]))
                ++pos_;
            if (pos_ >= xml_.size())
                return std::nullopt;
            if (xml_[pos_] == '>') {
                ++pos_;
                break;
            }
            if (xml_[pos_] == '/') {
                ++pos_;
                if (pos_ < xml_.size() && xml_[pos_] == '>')
                    ++pos_;
                tag.self_closing = true;
                break;
            }
            const std::size_t an = pos_;
            while (pos_ < xml_.size()
                   && !is_space(xml_[pos_])
                   && xml_[pos_] != '='
                   && xml_[pos_] != '>'
                   && xml_[pos_] != '/') {
                ++pos_;
            }
            const std::string_view name = xml_.substr(an, pos_ - an);
            while (pos_ < xml_.size() && is_space(xml_[pos_]))
                ++pos_;
            if (pos_ >= xml_.size() || xml_[pos_] != '=')
                continue;
            ++pos_;
            while (pos_ < xml_.size() && is_space(xml_[pos_]))
                ++pos_;
            if (pos_ >= xml_.size())
                return std::nullopt;
            const char quote = xml_[pos_];
            if (quote != '"' && quote != '\'')
                continue;
            ++pos_;
            const std::size_t vs = pos_;
            while (pos_ < xml_.size() && xml_[pos_] != quote)
                ++pos_;
            if (pos_ >= xml_.size())
                return std::nullopt;
            const std::string_view value = xml_.substr(vs, pos_ - vs);
            ++pos_;
            if (name.empty())
                continue;
            if (tag.attr_count < kMaxAttrs) {
                attrs[tag.attr_count++] = Attr{name, value};
            } else if (tag.attrs_dropped != 0xFF) {
                ++tag.attrs_dropped;
            }
        }
        Token t;
        t.kind = tag.closing ? Token::Kind::End : Token::Kind::Start;
        t.tag = tag;
        return t;
    }
}

std::optional<Tag> XmlScanner::next_tag(std::span<Attr, kMaxAttrs> attrs) noexcept {
    while (const auto t = next_token(attrs)) {
        if (t->kind == Token::Kind::Text || t->kind == Token::Kind::Markup)
            continue;
        return t->tag;
    }
    return std::nullopt;
}

}
