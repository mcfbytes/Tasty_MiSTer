// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mdp_cue.h"

#include <algorithm>
#include <cstddef>
#include <optional>

#include "infra/ascii.h"

namespace mister::cores::mdp {
namespace {

[[nodiscard]] svc::TrackCount count_through(svc::TrackNumber n) noexcept {
    return n.index() - svc::TrackIndex{} + svc::TrackCount{1};
}

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    std::size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t'))
        ++b;
    s.remove_prefix(b);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
        s.remove_suffix(1);
    }
    return s;
}

[[nodiscard]] std::int32_t parse_int(std::string_view s) noexcept {
    constexpr std::int64_t kMax = 0x7FFFFFFF;
    std::size_t i = 0;
    while (i < s.size()
           && (s[i] == ' '
               || s[i] == '\t'
               || s[i] == '\n'
               || s[i] == '\v'
               || s[i] == '\f'
               || s[i] == '\r')) {
        ++i;
    }
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        neg = s[i] == '-';
        ++i;
    }
    std::int64_t v = 0;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
        v = std::min(kMax, v * 10 + (s[i] - '0'));
    }
    return static_cast<std::int32_t>(neg ? -v : v);
}

void join_into(
    FixedStr<CueSheet::kPathMax, StrFit::Clip>& out,
    std::string_view dir,
    std::string_view name
) noexcept {
    (void)out.assign(dir);
    (void)out.append("/");
    (void)out.append(name);
}

[[nodiscard]] std::string_view next_record(std::string_view& text) noexcept {
    std::size_t n = std::min(text.size(), CueSheet::kLineMax);
    const std::size_t nl = text.substr(0, n).find('\n');
    if (nl != std::string_view::npos)
        n = nl + 1;
    const std::string_view rec = text.substr(0, n);
    text.remove_prefix(n);
    return rec;
}

}

void CueSheet::parse(std::string_view text, std::string_view base_dir) noexcept {
    tracks_ = {};
    num_tracks_ = svc::TrackCount{};

    std::optional<svc::TrackNumber> cur_track;
    FixedStr<kPathMax, StrFit::Clip> cur_file{};

    while (!text.empty()) {
        const std::string_view line = trim(next_record(text));
        if (line.empty())
            continue;

        if (infra::istarts_with(line, "FILE ")) {
            const std::size_t q1 = line.find('"');
            if (q1 == std::string_view::npos)
                continue;
            const std::size_t q2 = line.find('"', q1 + 1);
            if (q2 == std::string_view::npos)
                continue;
            join_into(cur_file, base_dir, line.substr(q1 + 1, q2 - q1 - 1));
        } else if (infra::istarts_with(line, "TRACK ")) {
            const std::int32_t tn = parse_int(line.substr(6));
            if (tn >= 1 && tn <= kMaxTracks) {
                cur_track = svc::TrackNumber::of(tn);
                num_tracks_ = std::max(num_tracks_, count_through(*cur_track));
            }
        } else if (infra::istarts_with(line, "REM NOLOOP")) {

            if (cur_track) {
                tracks_[*cur_track].loops = false;
                tracks_[*cur_track].loop_sector = 0;
            }
        } else if (infra::istarts_with(line, "REM LOOP")) {
            if (cur_track) {
                tracks_[*cur_track].loops = true;
                std::string_view tail = line.substr(8);
                while (!tail.empty() && (tail.front() == ' ' || tail.front() == '\t')) {
                    tail.remove_prefix(1);
                }
                const bool digit = !tail.empty() && tail.front() >= '0' && tail.front() <= '9';
                tracks_[*cur_track].loop_sector = digit
                    ? static_cast<std::uint32_t>(parse_int(tail))
                    : 0u;
            }
        } else if (infra::istarts_with(line, "INDEX 01") || infra::istarts_with(line, "INDEX 1 ")) {
            if (cur_track && !cur_file.empty()) {
                tracks_[*cur_track].wav_path = cur_file;

                tracks_[*cur_track].loops = true;
            }
        }
    }
}

}
