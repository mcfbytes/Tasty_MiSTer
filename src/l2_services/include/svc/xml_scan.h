// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mister::svc::xml {

inline constexpr std::size_t kMaxAttrs = 16;

struct Attr {
    std::string_view name;
    std::string_view value;
};

struct Tag {
    std::string_view name;
    bool closing = false;
    bool self_closing = false;

    std::uint8_t attrs_dropped = 0;
};

struct Token {
    enum class Kind : std::uint8_t { Text, Start, End, Markup };
    Kind kind = Kind::Text;
    std::string_view text;
    Tag tag;
};

[[nodiscard]] constexpr char lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
[[nodiscard]] constexpr bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

[[nodiscard]] bool iequal(std::string_view a, std::string_view b) noexcept;

[[nodiscard]] bool next_token(std::string_view xml, std::size_t& pos, Token& t, Attr* attrs,
                              std::size_t& attr_count) noexcept;

[[nodiscard]] bool next_tag(std::string_view xml, std::size_t& pos, Tag& tag, Attr* attrs,
                            std::size_t& attr_count) noexcept;

[[nodiscard]] std::string_view rbf_text(std::string_view xml) noexcept;

}  // namespace mister::svc::xml
