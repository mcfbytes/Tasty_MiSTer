// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/n64_db_match.h"
#include "cores/n64_db_tags.h"
#include "cores/n64_detect_flags.h"
#include "cores/n64_rom.h"
#include "infra/ascii.h"

namespace mister::cores::n64 {

inline constexpr std::array<std::string_view, 2> kDbFileNames = {
    "N64-database_user.txt",
    "N64-database.txt",
};

inline constexpr std::size_t kMd5HexChars = 32;
inline constexpr std::string_view kCartIdPrefix = "ID:";

namespace detail {

[[nodiscard]] constexpr bool line_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

}

[[nodiscard]] constexpr std::optional<std::string_view> md5_tail(
    std::string_view line,
    std::string_view md5_hex
) noexcept {
    if (md5_hex.size() < kMd5HexChars || line.size() < kMd5HexChars)
        return std::nullopt;
    for (std::size_t i = 0; i < kMd5HexChars; ++i)
        if (md5_hex[i] != infra::ascii_lower(line[i]))
            return std::nullopt;
    return line.substr(kMd5HexChars);
}

[[nodiscard]] constexpr std::size_t cart_id_match(
    std::string_view line,
    std::string_view cart_id
) noexcept {
    if (!line.starts_with(kCartIdPrefix))
        return 0;
    if (cart_id.size() < kCartIdChars)
        return 0;
    const std::string_view lp = line.substr(kCartIdPrefix.size());
    for (std::size_t i = 0; i < kCartIdChars && i < lp.size(); ++i) {
        if (i != 0 && infra::ascii_space(lp[i]))
            return i;
        if (lp[i] != '_' && lp[i] != cart_id[i])
            return 0;
    }
    return kCartIdChars;
}

[[nodiscard]] constexpr std::optional<std::string_view> cart_id_tail(
    std::string_view line,
    std::string_view cart_id
) noexcept {
    const std::size_t matched = cart_id_match(line, cart_id);
    if (matched == 0)
        return std::nullopt;
    const std::size_t at = kCartIdPrefix.size() + matched;
    return line.substr(at < line.size() ? at : line.size());
}

class DbKey {
public:
    [[nodiscard]] static constexpr DbKey md5(std::string_view hex) noexcept {
        return DbKey{Grammar::Md5, hex};
    }

    [[nodiscard]] static constexpr DbKey cart_id(std::string_view id) noexcept {
        return DbKey{Grammar::Cart, id};
    }

    [[nodiscard]] constexpr std::optional<std::string_view> tail(
        std::string_view line
    ) const noexcept {
        switch (grammar_) {
            case Grammar::Md5:
                return md5_tail(line, text_);
            case Grammar::Cart:
                return cart_id_tail(line, text_);
        }
        return std::nullopt;
    }

    friend constexpr bool operator==(const DbKey&, const DbKey&) = default;

private:
    enum class Grammar : std::uint8_t { Md5, Cart };

    constexpr DbKey(Grammar grammar, std::string_view text) noexcept
        : grammar_(grammar),
          text_(text) {}

    Grammar grammar_;
    std::string_view text_;
};

[[nodiscard]] constexpr std::optional<std::string_view> tags_of(std::string_view tail) noexcept {
    const std::size_t blanks = tail.find_first_not_of(" \t");
    if (blanks == 0 || blanks == std::string_view::npos)
        return std::nullopt;
    std::string_view body = tail.substr(blanks);
    const std::size_t comment = body.find_first_of("#;");
    if (comment != std::string_view::npos)
        body = body.substr(0, comment);
    if (body.empty())
        return std::nullopt;
    return body;
}

[[nodiscard]] constexpr std::optional<DbMatch> lookup(
    std::string_view text,
    const DbKey& key
) noexcept {
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = pos;
        while (end < text.size() && text[end] != '\0' && text[end] != '\r' && text[end] != '\n')
            ++end;
        std::size_t begin = pos;
        while (begin < end && detail::line_space(text[begin]))
            ++begin;
        pos = end + 1;

        const std::string_view line = text.substr(begin, end - begin);
        if (line.empty() || line.front() == '#' || line.front() == ';')
            continue;

        const auto tail = key.tail(line);
        if (!tail)
            continue;

        const auto body = tags_of(*tail);
        if (!body)
            return DbMatch{};
        return DbMatch{parse_tags(*body)};
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::optional<DbMatch> lookup_in_order(
    std::span<const std::string_view> texts,
    const DbKey& key
) noexcept {
    for (std::string_view text : texts)
        if (const auto match = lookup(text, key))
            return match;
    return std::nullopt;
}

[[nodiscard]] constexpr bool cart_id_lookupable(std::string_view cart_id) noexcept {
    if (cart_id.size() < kCartIdChars)
        return false;
    for (std::size_t i = 0; i < kCartIdChars; ++i)
        if (!infra::ascii_alnum(cart_id[i]))
            return false;
    return true;
}

[[nodiscard]] constexpr DetectFlags detect_level(const DbMatch& match, bool auto_detect) noexcept {
    const DetectFlags db = DetectFlags{}.with(DetectFlag::Db);
    if (!match.tags)
        return db;
    if (!auto_detect)
        return db.with(DetectFlag::Region);
    return match.tags->complete() ? db.with(DetectFlag::Region) : db;
}

}
