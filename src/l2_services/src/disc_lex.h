// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

#include "infra/error.h"

namespace mister::svc::disc::detail {

inline constexpr std::uint32_t kFramesPerSecond = 75;
inline constexpr std::size_t kSubcodeBytes = 96;
inline constexpr std::size_t kUserDataBytes = 2048;
inline constexpr std::uint32_t kMode2Cooked = 2336;
inline constexpr std::uint32_t kMaxCueTracks = 99;

inline char lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool ends_with_ci(std::string_view s, std::string_view suffix) noexcept {
    if (s.size() < suffix.size())
        return false;
    const std::size_t off = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (lower(s[off + i]) != lower(suffix[i]))
            return false;
    }
    return true;
}

inline std::string_view dir_of(std::string_view path) noexcept {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash + 1);
}

inline std::string_view lstrip(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    return s.substr(i);
}

inline constexpr std::size_t kNoDigitLimit = 0;
inline constexpr std::size_t kCueFieldDigits = 2;

inline std::optional<std::uint32_t> take_uint(
    std::string_view& s,
    std::size_t max_digits = kNoDigitLimit
) noexcept {
    s = lstrip(s);
    const std::size_t span = (max_digits == kNoDigitLimit) ? s.size()
                                                           : std::min(s.size(), max_digits);
    const char* first = s.data();
    const char* last = s.data() + span;
    std::uint32_t out = 0;
    const auto r = std::from_chars(first, last, out);
    if (r.ec != std::errc{} || r.ptr == first)
        return std::nullopt;
    s.remove_prefix(static_cast<std::size_t>(r.ptr - first));
    return out;
}

inline std::optional<std::uint32_t> take_msf(std::string_view s) noexcept {
    const auto m = take_uint(s, kCueFieldDigits);
    if (!m)
        return std::nullopt;
    if (s.empty() || s.front() != ':')
        return std::nullopt;
    s.remove_prefix(1);
    const auto sec = take_uint(s, kCueFieldDigits);
    if (!sec)
        return std::nullopt;
    if (s.empty() || s.front() != ':')
        return std::nullopt;
    s.remove_prefix(1);
    const auto f = take_uint(s, kCueFieldDigits);
    if (!f)
        return std::nullopt;
    return *m * 60 * kFramesPerSecond + *sec * kFramesPerSecond + *f;
}

class LineReader {
public:
    explicit LineReader(std::string_view text) noexcept : text_(text) {}

    [[nodiscard]] Ex<void> next(std::span<char> out) noexcept {
        if (out.empty())
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        do {
            std::size_t n = 0;
            out[0] = '\0';
            while (pos_ < text_.size() && text_[pos_] != '\n') {
                const char c = text_[pos_++];
                if (c == '\r')
                    continue;
                if (n + 1 < out.size()) {
                    out[n++] = c;
                    out[n] = '\0';
                }
            }
            if (pos_ < text_.size())
                ++pos_;
        } while (out[0] == '\0' && pos_ < text_.size());
        if (out[0] == '\0')
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
        return {};
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
};

}
