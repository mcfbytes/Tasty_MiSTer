// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_disc_key.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "cores/movie_refusal.h"
#include "cores/q_control.h"
#include "cores/types.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "infra/endian.h"
#include "infra/parse_int.h"
#include "proto/types.h"
#include "svc/chd_meta_row.h"
#include "svc/chd_source.h"
#include "svc/chd_track.h"
#include "svc/msf.h"
#include "svc/types.h"

namespace mister::cores {

using infra::ieq;

namespace {

using CR = MovieRefusal;

struct Fields {
    std::array<std::string_view, 4> f{};
    std::string_view rest{};
};

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    return s;
}

Fields fields(std::string_view s) {
    Fields out{};
    s = trim(s);
    for (std::size_t i = 0; i < out.f.size() && !s.empty(); ++i) {
        const std::size_t sp = std::min(s.find(' '), s.find('\t'));
        out.f[i] = s.substr(0, sp);
        s = trim(s.substr(out.f[i].size()));
        if (i == 0)
            out.rest = s;
    }
    return out;
}

struct TocTrack {
    svc::TrackNumber number{};
    QControl control{};
    bool mode2 = false;
    proto::BlockCount lba{};
};

struct CueTrack {
    TocTrack track{};
    proto::BlockCount index1{};
    std::size_t file = 0;
};

struct Cue {
    std::vector<std::string> files;
    std::vector<CueTrack> tracks;

    std::vector<bool> index1_seen;
};

[[nodiscard]] std::optional<proto::BlockCount> parse_cue_msf(std::string_view s) noexcept {
    constexpr std::size_t kDigits = 2;
    constexpr std::size_t kFields = 3;
    constexpr char kSeparator = ':';
    constexpr std::size_t kLen = kFields * kDigits + (kFields - 1);
    constexpr std::size_t kMinutesAt = 0;
    constexpr std::size_t kSecondsAt = kDigits + 1;
    constexpr std::size_t kFramesAt = kLen - kDigits;
    constexpr std::size_t kFirstSeparatorAt = kDigits;
    constexpr std::size_t kSecondSeparatorAt = kLen - kDigits - 1;
    if (s.size() != kLen
        || s[kFirstSeparatorAt] != kSeparator
        || s[kSecondSeparatorAt] != kSeparator)
        return std::nullopt;
    const auto m = infra::parse_int<std::uint32_t>(s.substr(kMinutesAt, kDigits));
    const auto sec = infra::parse_int<std::uint32_t>(s.substr(kSecondsAt, kDigits));
    const auto fr = infra::parse_int<std::uint32_t>(s.substr(kFramesAt, kDigits));
    if (!m || !sec || !fr || *sec >= svc::kSecondsPerMinute || *fr >= svc::kFramesPerSecond)
        return std::nullopt;
    return proto::BlockCount{(*m * svc::kSecondsPerMinute + *sec) * svc::kFramesPerSecond + *fr};
}

[[nodiscard]] Ex<Cue> parse_cue(std::string_view text) {
    Cue c;

    constexpr std::size_t kFirstArg = 1;
    constexpr std::size_t kTypeArg = 2;
    constexpr std::size_t kMaxNumberDigits = 2;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        const Fields fl = fields(text.substr(0, nl));
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        const std::string_view kw = fl.f[0];
        if (ieq(kw, "FILE")) {
            std::string_view name;
            std::string_view type;
            if (!fl.rest.empty() && fl.rest.front() == '"') {
                const std::size_t q = fl.rest.find('"', 1);
                if (q == std::string_view::npos)
                    return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
                name = fl.rest.substr(1, q - 1);
                type = fields(fl.rest.substr(q + 1)).f[0];
            } else {
                name = fl.f[kFirstArg];
                type = fl.f[kTypeArg];
            }
            if (name.empty() || !ieq(type, "BINARY") || c.files.size() == PsxDiscKey::kMaxFiles)
                return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
            c.files.emplace_back(name);
        } else if (ieq(kw, "TRACK")) {
            const auto n = fl.f[kFirstArg].size() <= kMaxNumberDigits
                ? infra::parse_int<std::uint32_t>(fl.f[kFirstArg])
                : std::nullopt;
            const std::string_view type = fl.f[kTypeArg];
            if (c.files.empty() || !n || *n != c.tracks.size() + 1)
                return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
            CueTrack t{.file = c.files.size() - 1};
            t.track.number = svc::TrackNumber::of(*n);
            if (ieq(type, "MODE2/2352") || ieq(type, "MODE1/2352")) {
                t.track.control = t.track.control.with_data();
                t.track.mode2 = ieq(type, "MODE2/2352");
            } else if (!ieq(type, "AUDIO")) {
                return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
            }
            c.tracks.push_back(t);
            c.index1_seen.push_back(false);
        } else if (ieq(kw, "INDEX")) {
            const auto n = fl.f[kFirstArg].size() <= kMaxNumberDigits
                ? infra::parse_int<std::uint32_t>(fl.f[kFirstArg])
                : std::nullopt;
            const auto at = parse_cue_msf(fl.f[kTypeArg]);
            if (c.tracks.empty() || !n || !at)
                return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
            CueTrack& t = c.tracks.back();

            if (*n == 1 && t.file == c.files.size() - 1) {
                t.index1 = *at;
                c.index1_seen.back() = true;
            } else if (*n == 1) {
                return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
            }
        } else if (ieq(kw, "FLAGS")) {
            if (c.tracks.empty())
                return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
            QControl& control = c.tracks.back().track.control;
            for (std::size_t i = 1; i < fl.f.size() && !fl.f[i].empty(); ++i) {
                const std::string_view flag = fl.f[i];
                if (ieq(flag, "PRE")) {
                    control = control.with_pre();
                } else if (ieq(flag, "DCP")) {
                    control = control.with_dcp();
                } else if (ieq(flag, "4CH")) {
                    control = control.with_four_channel();
                } else if (!ieq(flag, "SCMS")) {
                    return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
                }
            }
        } else if (ieq(kw, "PREGAP") || ieq(kw, "POSTGAP")) {
            return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
        }
    }
    if (c.tracks.empty() || c.tracks.size() > PsxDiscKey::kMaxFiles)
        return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
    for (const bool seen : c.index1_seen)
        if (!seen)
            return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));

    const CueTrack& t1 = c.tracks.front();
    if (!t1.track.control.is_data() || t1.file != 0 || t1.index1 != proto::BlockCount{})
        return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
    return c;
}

struct SessionFormat {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr SessionFormat of(bool any_mode2) noexcept {
        return SessionFormat{
            static_cast<std::uint32_t>(any_mode2 ? DiscType::CdRomXa : DiscType::CdRom)
        };
    }

    constexpr void store_le32(std::span<std::uint8_t, 4> dst) const noexcept {
        infra::store_le<std::uint32_t>(dst, v);
    }
};

template <class W>
void put_toc_word(std::span<std::uint8_t, 4> dst, const W& w) {
    if constexpr (std::is_integral_v<W>)
        infra::store_le<std::uint32_t>(dst, static_cast<std::uint32_t>(w));
    else
        w.store_le32(dst);
}

template <class A, class B, class C>
std::array<std::uint8_t, 12> le32x3(const A& a, const B& b, const C& c) {
    std::array<std::uint8_t, 12> out{};
    const std::span<std::uint8_t, 12> all(out);
    put_toc_word(all.subspan<0, 4>(), a);
    put_toc_word(all.subspan<4, 4>(), b);
    put_toc_word(all.subspan<8, 4>(), c);
    return out;
}

std::vector<std::uint8_t> toc_bytes(std::span<const TocTrack> tracks, proto::BlockCount lead_out) {
    std::vector<std::uint8_t> out;
    out.reserve(PsxDiscKey::kTocPrefixBytes);
    const auto add = [&out](const std::array<std::uint8_t, 12>& e) {
        out.insert(out.end(), e.begin(), e.end());
    };

    const bool xa = std::any_of(tracks.begin(), tracks.end(), [](const TocTrack& t) {
        return t.mode2;
    });
    add(le32x3(SessionFormat::of(xa), tracks.front().number, tracks.back().number));

    constexpr std::size_t kTocEntries = 100;
    for (std::size_t i = 1; i <= kTocEntries; ++i) {
        if (i <= tracks.size()) {
            const TocTrack& t = tracks[i - 1];
            add(le32x3(t.control, 1, t.lba));
        } else if (i == kTocEntries) {
            add(le32x3(0, 1, lead_out));
        } else {
            add(le32x3(0, 0, 0));
        }
    }
    return out;
}

}

Ex<std::vector<std::string>> PsxDiscKey::cue_files(std::string_view cue) {
    const auto c = parse_cue(cue);
    if (!c)
        return std::unexpected(c.error());
    return c->files;
}

Ex<std::vector<std::uint8_t>> PsxDiscKey::toc_prefix(
    std::string_view cue,
    std::span<const proto::FileSize> file_sizes
) {
    const auto c = parse_cue(cue);
    if (!c)
        return std::unexpected(c.error());
    if (file_sizes.size() != c->files.size())
        return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));

    constexpr proto::BlockSize kSectorSize{svc::kRawFrameBytes};
    std::vector<proto::BlockCount> base(c->files.size() + 1);
    for (std::size_t i = 0; i < c->files.size(); ++i)
        base[i + 1] = base[i] + file_sizes[i].whole_units(kSectorSize);
    if (file_sizes.front() < proto::FileSize{kKeySectors * kSectorBytes})
        return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
    std::vector<TocTrack> toc;
    toc.reserve(c->tracks.size());
    for (const CueTrack& ct : c->tracks) {
        TocTrack t = ct.track;
        t.lba = base[ct.file] + ct.index1;
        toc.push_back(t);
    }
    return toc_bytes(toc, base.back());
}

Ex<std::vector<std::uint8_t>> PsxDiscKey::chd_key(svc::IChdSource& chd) {

    std::vector<TocTrack> toc;
    proto::BlockCount at{};
    proto::BlockCount first_frames{};
    for (std::uint32_t i = 0;; ++i) {
        const auto row = chd.track_metadata(svc::TrackIndex::of(i));
        if (!row)
            break;
        const auto t = svc::parse_chd_track(*row);
        if (!t || !t->number.matches(svc::CueField{i + 1}) || i == kMaxFiles)
            return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
        TocTrack e{.number = t->number};

        if (t->sector_size != svc::ChdSectorSize::Raw)
            return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
        if (t->type != svc::TrackType::Cdda) {
            e.control = e.control.with_data();
            e.mode2 = t->type == svc::TrackType::Mode2;
        }

        if ((t->pregap != proto::BlockCount{} && !t->pregap_held)
            || t->postgap != proto::BlockCount{})
            return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
        e.lba = at + t->pregap;
        at += t->frames;
        if (i == 0)
            first_frames = t->frames;
        toc.push_back(e);
    }
    if (toc.empty()
        || !toc.front().control.is_data()
        || toc.front().lba != proto::BlockCount{}
        || first_frames < proto::BlockCount{kKeySectors})
        return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
    std::vector<std::uint8_t> out = toc_bytes(toc, at);

    const svc::IChdSource::Geometry geo = chd.geometry();
    if (geo.unit_bytes != proto::BlockSize{kChdFrameBytes}
        || geo.hunk_bytes < geo.unit_bytes.extent(proto::BlockCount{1})
        || geo.hunk_bytes > proto::FileSize{kChdHunkMax})
        return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
    std::vector<std::byte> hunk(geo.hunk_extent());
    std::optional<svc::HunkIndex> held_hunk{};
    out.reserve(out.size() + kKeySectors * kSectorBytes);
    for (std::uint32_t f = 0; f < kKeySectors; ++f) {
        const svc::ChdUnit unit{f};
        const svc::HunkIndex wanted = geo.hunk_of(unit);
        if (held_hunk != wanted) {
            if (auto r = chd.read_hunk(wanted, hunk); !r)
                return std::unexpected(r.error());
            held_hunk = wanted;
        }
        const auto* p = infra::byte_cast<const std::uint8_t>(std::span(hunk)).data()
            + geo.offset_in_hunk(unit);
        out.insert(out.end(), p, p + kSectorBytes);
    }
    return out;
}

Error PsxDiscKey::chd_open_error(const Error& e) noexcept {
    if (e.code == Errc::mount_failed)
        return movie_refusal(CR::ChdUnsupported, ERR_SITE());
    if (e.code == Errc::bad_format)
        return movie_refusal(CR::Disk, ERR_SITE());
    return e;
}

}
