// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "infra/ascii.h"

namespace mister::proto {

enum class StandardItem : std::uint8_t { PauseOnOsd, kCount };
enum class ItemShape : std::uint8_t { Toggle };

inline constexpr std::size_t kMaxStandardSpellings = 4;

struct StandardItemRow {
    StandardItem item;

    std::array<std::string_view, kMaxStandardSpellings> labels;
    ItemShape shape;
};

inline constexpr std::array<StandardItemRow, std::to_underlying(StandardItem::kCount)>
    kStandardItemRows{{
        {StandardItem::PauseOnOsd,
         {"pause when osd open", "osd pause", "pause on osd", "pause core on osd"},
         ItemShape::Toggle},
    }};

namespace detail {

[[nodiscard]] constexpr bool standard_label_word_byte(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

[[nodiscard]] constexpr std::size_t standard_label_word_start(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size() && !standard_label_word_byte(s[i]))
        ++i;
    return i;
}

[[nodiscard]] constexpr std::string_view standard_label_first_word(std::string_view s) noexcept {
    const std::size_t first = standard_label_word_start(s);
    std::size_t last = first;
    while (last < s.size() && standard_label_word_byte(s[last]))
        ++last;
    return s.substr(first, last - first);
}

[[nodiscard]] constexpr std::string_view standard_label_after_first_word(
    std::string_view s
) noexcept {
    return s.substr(standard_label_word_start(s) + standard_label_first_word(s).size());
}

[[nodiscard]] constexpr bool standard_label_matches(
    std::string_view raw,
    std::string_view normalised
) noexcept {
    for (;;) {
        while (infra::ieq(standard_label_first_word(raw), "is"))
            raw = standard_label_after_first_word(raw);
        const std::string_view rw = standard_label_first_word(raw);
        const std::string_view nw = standard_label_first_word(normalised);
        if (rw.empty() || nw.empty())
            return rw.empty() && nw.empty();
        if (!infra::ieq(rw, nw))
            return false;
        raw = standard_label_after_first_word(raw);
        normalised = standard_label_after_first_word(normalised);
    }
}

[[nodiscard]] constexpr bool standard_label_is_normalised(std::string_view s) noexcept {
    if (s.empty() || s.front() == ' ' || s.back() == ' ')
        return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const bool word = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!word && !(c == ' ' && s[i - 1] != ' '))
            return false;
    }
    for (; !standard_label_first_word(s).empty(); s = standard_label_after_first_word(s)) {
        if (standard_label_first_word(s) == "is")
            return false;
    }
    return true;
}

[[nodiscard]] constexpr bool standard_row_matches(
    const StandardItemRow& row,
    std::string_view raw
) noexcept {
    for (std::string_view label : row.labels) {
        if (!label.empty() && standard_label_matches(raw, label))
            return true;
    }
    return false;
}

consteval bool standard_rows_in_ordinal_order() {
    for (std::size_t i = 0; i < kStandardItemRows.size(); ++i) {
        if (std::to_underlying(kStandardItemRows[i].item) != i)
            return false;
    }
    return true;
}

consteval bool standard_rows_labels_normalised() {
    for (const StandardItemRow& r : kStandardItemRows) {
        bool trailing = false;
        for (std::string_view label : r.labels) {
            if (label.empty()) {
                trailing = true;
                continue;
            }
            if (trailing || !standard_label_is_normalised(label))
                return false;
            if (!standard_label_matches(label, label))
                return false;
        }
        if (r.labels[0].empty())
            return false;
    }
    return true;
}

consteval bool standard_rows_labels_distinct() {
    constexpr std::size_t n = kStandardItemRows.size() * kMaxStandardSpellings;
    for (std::size_t i = 0; i < n; ++i) {
        const std::string_view a = kStandardItemRows[i / kMaxStandardSpellings]
                                       .labels[i % kMaxStandardSpellings];
        for (std::size_t j = i + 1; j < n && !a.empty(); ++j) {
            if (a == kStandardItemRows[j / kMaxStandardSpellings].labels[j % kMaxStandardSpellings])
                return false;
        }
    }
    return true;
}

}

static_assert(detail::standard_rows_in_ordinal_order(), "one row per StandardItem, in order");
static_assert(
    detail::standard_rows_labels_normalised(),
    "every row label is normalised, empties trailing"
);
static_assert(
    detail::standard_rows_labels_distinct(),
    "no label appears twice, in a row or across rows"
);

}
