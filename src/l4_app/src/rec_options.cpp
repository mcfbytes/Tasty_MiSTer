// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rec_options.h"

#include <charconv>
#include <cstdio>
#include <limits>

#include "infra/ascii.h"
#include "infra/parse_int.h"

namespace mister::app {

const char* rec_codec_name(RecCodec c) noexcept {
    return rec_codec_facts(c).name.data();
}

bool rec_codec_available(RecCodec c) noexcept {
    return rec_codec_facts(c).available;
}

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
    for (const RecCodecFacts& row : kRecCodecFacts)
        if (infra::ieq(s, row.name))
            return row.kind;
    return std::nullopt;
}

std::optional<RecScale> parse_rec_scale(std::string_view s) noexcept {
    if (infra::ieq(s, "auto"))
        return RecScale::Auto;
    if (infra::ieq(s, "native"))
        return RecScale::Native;
    if (infra::ieq(s, "half"))
        return RecScale::Half;
    return std::nullopt;
}

std::optional<RecMotion> parse_rec_motion(std::string_view s) noexcept {
    if (infra::ieq(s, "auto"))
        return RecMotion::Auto;
    if (infra::ieq(s, "off"))
        return RecMotion::Off;
    if (infra::ieq(s, "small"))
        return RecMotion::Small;
    if (infra::ieq(s, "full"))
        return RecMotion::Full;
    return std::nullopt;
}

std::optional<RecEvery> parse_rec_every(std::string_view s) noexcept {
    const auto v = infra::parse_int<unsigned>(s);
    if (!v)
        return std::nullopt;
    return RecEvery::of(*v);
}

std::optional<proto::FileSize> parse_rec_size(std::string_view s) noexcept {
    if (s.empty())
        return std::nullopt;
    std::uint64_t v = 0;
    const char* const begin = s.data();
    const char* const end = begin + s.size();
    const auto [p, ec] = std::from_chars(begin, end, v);
    if (ec != std::errc{})
        return std::nullopt;
    std::uint64_t mul = 1;
    if (p != end) {
        if (p + 1 != end)
            return std::nullopt;
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
    if (mul != 1 && v > std::numeric_limits<std::uint64_t>::max() / mul)
        return std::nullopt;
    return proto::FileSize{v * mul};
}

FixedStr<160, StrFit::Reject> format_rec_options(const RecOptions& o) noexcept {
    char buf[160];
    const int n = std::snprintf(
        buf,
        sizeof buf,
        "codec=%s scale=%s motion=%s every=%u from=%s to=%s segment=%s",
        rec_codec_name(o.codec),
        rec_scale_name(o.scale),
        rec_motion_name(o.motion),
        o.every.count<unsigned>(),
        o.from_frame.text().c_str(),
        o.to_frame.text().c_str(),
        o.segment_bytes.text().c_str()
    );
    FixedStr<160, StrFit::Reject> out;
    if (n > 0 && static_cast<std::size_t>(n) < sizeof buf)
        (void)out.assign(std::string_view(buf, static_cast<std::size_t>(n)));
    return out;
}

}
