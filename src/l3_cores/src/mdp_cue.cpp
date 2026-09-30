// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mdp_cue.h"

#include <algorithm>
#include <cstddef>

namespace mister::cores::mdp {
namespace {

constexpr char lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool iprefix(std::string_view line, std::string_view lit) noexcept {
    if (line.size() < lit.size()) return false;
    for (std::size_t i = 0; i < lit.size(); ++i) {
        if (lower(line[i]) != lower(lit[i])) return false;
    }
    return true;
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
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\v' ||
                            s[i] == '\f' || s[i] == '\r')) {
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

void join_into(FixedStr<CueSheet::kPathMax, StrFit::Clip>& out, std::string_view dir,
               std::string_view name) noexcept {
    char buf[CueSheet::kPathMax];
    std::size_t n = 0;
    const auto put = [&](std::string_view s) noexcept {
        for (const char c : s) {
            if (n + 1 < sizeof buf) buf[n++] = c;
        }
    };
    put(dir);
    put("/");
    put(name);
    (void)out.assign(std::string_view{buf, n});
}

[[nodiscard]] std::string_view next_record(std::string_view& text) noexcept {
    std::size_t n = std::min(text.size(), CueSheet::kLineMax);
    const std::size_t nl = text.substr(0, n).find('\n');
    if (nl != std::string_view::npos) n = nl + 1;
    const std::string_view rec = text.substr(0, n);
    text.remove_prefix(n);
    return rec;
}

}  // namespace

void CueSheet::parse(std::string_view text, std::string_view base_dir) noexcept {
    tracks_ = {};
    num_tracks_ = 0;

    std::uint8_t cur_track = 0;
    FixedStr<kPathMax, StrFit::Clip> cur_file{};

    while (!text.empty()) {
        const std::string_view line = trim(next_record(text));
        if (line.empty()) continue;

        if (iprefix(line, "FILE ")) {
            const std::size_t q1 = line.find('"');
            if (q1 == std::string_view::npos) continue;
            const std::size_t q2 = line.find('"', q1 + 1);
            if (q2 == std::string_view::npos) continue;
            join_into(cur_file, base_dir, line.substr(q1 + 1, q2 - q1 - 1));
        } else if (iprefix(line, "TRACK ")) {
            const std::int32_t tn = parse_int(line.substr(6));
            if (tn >= 1 && tn <= kMaxTracks) {
                cur_track = static_cast<std::uint8_t>(tn);
                num_tracks_ = std::max(num_tracks_, cur_track);
            }
        } else if (iprefix(line, "REM NOLOOP")) {

            if (cur_track >= 1) {
                tracks_[cur_track].loops = false;
                tracks_[cur_track].loop_sector = 0;
            }
        } else if (iprefix(line, "REM LOOP")) {
            if (cur_track >= 1) {
                tracks_[cur_track].loops = true;
                std::string_view tail = line.substr(8);
                while (!tail.empty() && (tail.front() == ' ' || tail.front() == '\t')) {
                    tail.remove_prefix(1);
                }
                const bool digit = !tail.empty() && tail.front() >= '0' && tail.front() <= '9';
                tracks_[cur_track].loop_sector =
                    digit ? static_cast<std::uint32_t>(parse_int(tail)) : 0u;
            }
        } else if (iprefix(line, "INDEX 01") || iprefix(line, "INDEX 1 ")) {
            if (cur_track >= 1 && !cur_file.empty()) {
                tracks_[cur_track].wav_path = cur_file;

                tracks_[cur_track].loops = true;
            }
        }
    }
}

}  // namespace mister::cores::mdp
