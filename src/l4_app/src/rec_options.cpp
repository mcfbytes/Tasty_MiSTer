// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rec_options.h"

#include <cctype>
#include <charconv>
#include <cstdio>
#include <limits>

namespace mister::app {
namespace {

bool eq_ci(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

}  // namespace

const char* rec_codec_name(RecCodec c) noexcept { return c == RecCodec::Zmbv ? "zmbv" : "cscd"; }

const char* rec_scale_name(RecScale s) noexcept {
    switch (s) {
        case RecScale::Native:
            return "native";
        case RecScale::Half:
            return "half";
        case RecScale::Auto:
            return "auto";
    }
    return "auto";
}

const char* rec_motion_name(RecMotion m) noexcept {
    switch (m) {
        case RecMotion::Off:
            return "off";
        case RecMotion::Small:
            return "small";
        case RecMotion::Full:
            return "full";
        case RecMotion::Auto:
            return "auto";
    }
    return "auto";
}

std::optional<RecCodec> parse_rec_codec(std::string_view s) noexcept {
    if (eq_ci(s, "cscd")) return RecCodec::Cscd;
    if (eq_ci(s, "zmbv")) return RecCodec::Zmbv;
    return std::nullopt;
}

std::optional<RecScale> parse_rec_scale(std::string_view s) noexcept {
    if (eq_ci(s, "auto")) return RecScale::Auto;
    if (eq_ci(s, "native")) return RecScale::Native;
    if (eq_ci(s, "half")) return RecScale::Half;
    return std::nullopt;
}

std::optional<RecMotion> parse_rec_motion(std::string_view s) noexcept {
    if (eq_ci(s, "auto")) return RecMotion::Auto;
    if (eq_ci(s, "off")) return RecMotion::Off;
    if (eq_ci(s, "small")) return RecMotion::Small;
    if (eq_ci(s, "full")) return RecMotion::Full;
    return std::nullopt;
}

std::optional<std::uint16_t> parse_rec_every(std::string_view s) noexcept {
    unsigned v = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size() || v == 0 || v > kRecEveryMax)
        return std::nullopt;
    return static_cast<std::uint16_t>(v);
}

std::optional<std::uint64_t> parse_rec_size(std::string_view s) noexcept {
    if (s.empty()) return std::nullopt;
    std::uint64_t v = 0;
    const char* const begin = s.data();
    const char* const end = begin + s.size();
    const auto [p, ec] = std::from_chars(begin, end, v);
    if (ec != std::errc{}) return std::nullopt;
    std::uint64_t mul = 1;
    if (p != end) {
        if (p + 1 != end) return std::nullopt;
        const char c = *p;
        if (c == 'k' || c == 'K')
            mul = 1024ull;
        else if (c == 'm' || c == 'M')
            mul = 1024ull * 1024ull;
        else if (c == 'g' || c == 'G')
            mul = 1024ull * 1024ull * 1024ull;
        else
            return std::nullopt;
    }
    if (mul != 1 && v > std::numeric_limits<std::uint64_t>::max() / mul) return std::nullopt;
    return v * mul;
}

FixedStr<160, StrFit::Reject> format_rec_options(const RecOptions& o) noexcept {
    char buf[160];
    const unsigned every = o.every == 0 ? 1u : o.every;
    const int n = std::snprintf(
        buf, sizeof buf, "codec=%s scale=%s motion=%s every=%u from=%d to=%d segment=%llu",
        rec_codec_name(o.codec), rec_scale_name(o.scale), rec_motion_name(o.motion), every,
        o.from_frame, o.to_frame, static_cast<unsigned long long>(o.segment_bytes));
    FixedStr<160, StrFit::Reject> out;
    if (n > 0 && static_cast<std::size_t>(n) < sizeof buf)
        (void)out.assign(std::string_view(buf, static_cast<std::size_t>(n)));
    return out;
}

}  // namespace mister::app
