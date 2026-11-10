// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/cmd_line.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

#include "app/cmd_verb.h"
#include "app/xml_kind.h"
#include "infra/parse_int.h"
#include "proto/types.h"

namespace mister::app {
namespace {

bool starts_with(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string_view after(std::string_view s, std::string_view prefix) {
    return s.substr(prefix.size());
}

constexpr std::string_view kVideoModePrefix = "video_mode ";
constexpr std::string_view kLoadCorePrefix = "load_core ";
constexpr std::string_view kScreenshotPrefix = "screenshot";
constexpr std::string_view kScaledWord = "scaled";
constexpr std::string_view kVolumePrefix = "volume ";
constexpr std::string_view kTasPlayPrefix = "tas_play ";
constexpr std::string_view kRecStartPrefix = "rec_start ";
constexpr std::string_view kRecArmPrefix = "rec_arm ";

bool is_blank(char c) {
    return c == ' ' || c == '\t';
}

std::string_view lstrip(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && is_blank(s[i]))
        ++i;
    return s.substr(i);
}

std::optional<std::string_view> next_token(std::string_view& rest) {
    rest = lstrip(rest);
    if (rest.empty())
        return std::nullopt;
    if (rest.front() == '"') {
        const std::size_t close = rest.find('"', 1);
        if (close == std::string_view::npos || close == 1)
            return std::nullopt;
        const std::string_view tok = rest.substr(1, close - 1);
        rest.remove_prefix(close + 1);
        if (!rest.empty() && !is_blank(rest.front()))
            return std::nullopt;
        return tok;
    }
    std::size_t n = 0;
    while (n < rest.size() && !is_blank(rest[n]))
        ++n;
    const std::string_view tok = rest.substr(0, n);
    rest.remove_prefix(n);
    return tok;
}

std::optional<std::int32_t> small_int(std::string_view s) {
    constexpr std::size_t kMaxSmallIntDigits = 9;
    if (s.size() > 1 && s.front() == '+' && s[1] != '-')
        s.remove_prefix(1);
    const std::size_t digits = !s.empty() && s.front() == '-' ? s.size() - 1 : s.size();
    if (digits > kMaxSmallIntDigits)
        return std::nullopt;
    return infra::parse_int<std::int32_t>(s);
}

std::optional<CmdVerb::TasPlay> parse_tas_play(std::string_view args) {
    CmdVerb::TasPlay v{};
    const auto movie = next_token(args);
    const auto rom = next_token(args);
    if (!movie || !rom)
        return std::nullopt;
    v.movie = *movie;
    v.rom = *rom;
    if (const auto phase = next_token(args)) {
        const auto p = small_int(*phase);
        if (!p || *p < 0)
            return std::nullopt;
        v.phase = std::chrono::microseconds{*p};
        if (const auto lead = next_token(args)) {
            const auto l = small_int(*lead);
            if (!l)
                return std::nullopt;
            v.lead = FrameCount{*l};
        }
    }
    if (!lstrip(args).empty())
        return std::nullopt;
    return v;
}

std::optional<CmdVerb::RecStart> parse_rec(std::string_view args) {
    const auto path = next_token(args);
    if (!path || path->empty())
        return std::nullopt;
    CmdVerb::RecStart v;
    v.path = *path;
    bool saw_mode = false;
    unsigned seen = 0;
    constexpr unsigned kCodec = 1u, kScale = 2u, kEvery = 4u, kFrom = 8u, kTo = 16u, kSeg = 32u,
                       kMotion = 64u;
    while (const auto tok = next_token(args)) {
        const auto eq = tok->find('=');
        if (eq == std::string_view::npos) {
            if (saw_mode)
                return std::nullopt;
            if (*tok == "hash")
                v.mode = RecMode::Hash;
            else if (*tok != "avi")
                return std::nullopt;
            saw_mode = true;
            continue;
        }
        const std::string_view key = tok->substr(0, eq);
        const std::string_view val = tok->substr(eq + 1);
        if (val.empty())
            return std::nullopt;
        const auto take = [&](unsigned bit) {
            if ((seen & bit) != 0)
                return false;
            seen |= bit;
            return true;
        };
        if (key == "codec") {
            const auto c = parse_rec_codec(val);
            if (!c || !take(kCodec))
                return std::nullopt;
            v.opt.codec = *c;
        } else if (key == "scale") {
            const auto s = parse_rec_scale(val);
            if (!s || !take(kScale))
                return std::nullopt;
            v.opt.scale = *s;
        } else if (key == "motion") {
            const auto m = parse_rec_motion(val);
            if (!m || !take(kMotion))
                return std::nullopt;
            v.opt.motion = *m;
        } else if (key == "every") {
            const auto n = parse_rec_every(val);
            if (!n || !take(kEvery))
                return std::nullopt;
            v.opt.every = *n;
        } else if (key == "from" || key == "to") {
            const auto n = infra::parse_int<std::int32_t>(val);
            if (!n)
                return std::nullopt;
            const unsigned bit = key == "from" ? kFrom : kTo;
            if (!take(bit))
                return std::nullopt;
            if (key == "from")
                v.opt.from_frame = MovieFrame{*n};
            else
                v.opt.to_frame = MovieFrame{*n};
        } else if (key == "segment") {
            const auto n = parse_rec_size(val);
            if (!n || !rec_segment_ok(*n) || !take(kSeg))
                return std::nullopt;
            v.opt.segment_bytes = *n;
        } else {
            return std::nullopt;
        }
    }
    if (!lstrip(args).empty() || !rec_bounds_ok(v.opt))
        return std::nullopt;
    return v;
}

CmdLineOutcome taken(bool t) noexcept {
    return t ? CmdLineOutcome::Routed : CmdLineOutcome::Unrouted;
}

}

CmdLineOutcome deliver_cmd_line(std::string_view line, ICmdVerbSink& sink) noexcept {
    constexpr char kVolumeDigitMax = '7';

    if (starts_with(line, "fb_cmd"))
        return taken(sink.on(CmdVerb::FbCmd{line}));

    if (starts_with(line, kVideoModePrefix))
        return taken(sink.on(CmdVerb::VideoMode{after(line, kVideoModePrefix)}));

    if (starts_with(line, kLoadCorePrefix)) {
        const std::string_view path = after(line, kLoadCorePrefix);
        switch (xml_kind_of(line)) {
            case XmlKind::Mgl:
                return taken(sink.on(CmdVerb::Playlist{path}));
            case XmlKind::Mra:
                return taken(sink.on(CmdVerb::LoadCore{path, CmdVerb::RbfOrMra::Mra}));
            case XmlKind::Rbf:
                return taken(sink.on(CmdVerb::LoadCore{path, CmdVerb::RbfOrMra::Rbf}));
        }
        return CmdLineOutcome::Unrecognised;
    }

    if (starts_with(line, kScreenshotPrefix)) {
        std::string_view rest = lstrip(after(line, kScreenshotPrefix));
        bool scaled = false;
        if (starts_with(rest, kScaledWord)
            && (rest.size() == kScaledWord.size() || is_blank(rest[kScaledWord.size()]))) {
            scaled = true;
            rest = lstrip(after(rest, kScaledWord));
        }
        return taken(sink.on(CmdVerb::Screenshot{rest, scaled}));
    }

    if (starts_with(line, kVolumePrefix)) {
        const std::string_view arg = after(line, kVolumePrefix);
        if (arg == "mute")
            return taken(sink.on(CmdVerb::VolumeMute{true}));
        if (arg == "unmute")
            return taken(sink.on(CmdVerb::VolumeMute{false}));
        if (arg.size() == 1 && arg[0] >= '0' && arg[0] <= kVolumeDigitMax) {

            const int n = arg[0] - '0';
            return taken(sink.on(
                CmdVerb::VolumeAttenuation{proto::Attenuation::of(proto::Attenuation::kMax - n)}
            ));
        }
        return CmdLineOutcome::Unrecognised;
    }

    if (line == "rtstats")
        return taken(sink.on(CmdVerb::RtStats{}));

    if (starts_with(line, kTasPlayPrefix)) {
        const auto v = parse_tas_play(after(line, kTasPlayPrefix));
        if (!v)
            return CmdLineOutcome::Unrecognised;
        return taken(sink.on(*v));
    }
    if (line == "tas_stop")
        return taken(sink.on(CmdVerb::TasStop{}));

    if (starts_with(line, kRecStartPrefix) || starts_with(line, kRecArmPrefix)) {
        const bool arm = starts_with(line, kRecArmPrefix);
        const auto v = parse_rec(after(line, arm ? kRecArmPrefix : kRecStartPrefix));
        if (!v)
            return CmdLineOutcome::Unrecognised;
        if (arm)
            return taken(sink.on(CmdVerb::RecArm{v->path, v->mode, v->opt}));
        return taken(sink.on(*v));
    }
    if (line == "rec_stop")
        return taken(sink.on(CmdVerb::RecStop{}));
    if (line == "rec_disarm")
        return taken(sink.on(CmdVerb::RecDisarm{}));

    return CmdLineOutcome::Unrecognised;
}

}
