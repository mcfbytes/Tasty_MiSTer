// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disc_engine.h"

#include "svc/chd_prefetch.h"
#include "svc/index_pair_cue_program.h"
#include "svc/index_triple_cue_program.h"
#include "svc/rw_interleaver.h"
#include "svc/sector_descrambler.h"
#include "svc/sentinel_cue_program.h"

#include "infra/byte_view.h"

#include <cstring>
#include <string>

#include "disc_lex.h"
#include "vfs_detail.h"

namespace mister::svc {

using namespace disc::detail;

namespace {

std::uint8_t bcd(std::uint32_t v) noexcept {
    return static_cast<std::uint8_t>((((v / 10) % 10) << 4) | (v % 10));
}

struct ChdTypeRow {
    std::string_view name;
    std::uint32_t sector_size;
    TrackType type;
};

constexpr ChdTypeRow kChdTypes[] = {
    {"MODE1_RAW", 2352, TrackType::Mode1},
    {"MODE2_RAW", 2352, TrackType::Mode2},
    {"MODE1", 2048, TrackType::Mode1},
    {"MODE2", kMode2Cooked, TrackType::Mode2},
    {"AUDIO", 2352, TrackType::Cdda},
};

bool take_key(std::string_view& s, std::string_view key) noexcept {
    s = lstrip(s);
    if (!s.starts_with(key))
        return false;
    s.remove_prefix(key.size());
    return true;
}

struct Taken {
    std::size_t written = 0;
    std::string_view rest;
};

[[nodiscard]] Taken take_token(std::span<char> out, std::string_view s) noexcept {
    s = lstrip(s);
    std::size_t n = 0;
    while (n + 1 < out.size() && n < s.size() && s[n] != ' ' && s[n] != '\t') {
        out[n] = s[n];
        ++n;
    }
    if (!out.empty())
        out[n] = '\0';
    s.remove_prefix(n);
    return Taken{n, s};
}

}

std::optional<ChdTrack> parse_chd_track(const ChdMetaRow& row) noexcept {
    std::string_view s = row.text;
    ChdTrack out{};
    char pgsub[32]{};
    if (!take_key(s, "TRACK:"))
        return std::nullopt;
    const auto number = take_uint(s);
    if (!number)
        return std::nullopt;
    out.number = *number;
    if (!take_key(s, "TYPE:"))
        return std::nullopt;
    const Taken type = take_token(out.type, s);
    if (type.written == 0)
        return std::nullopt;
    s = type.rest;
    if (!take_key(s, "SUBTYPE:"))
        return std::nullopt;
    const Taken subtype = take_token(out.subtype, s);
    if (subtype.written == 0)
        return std::nullopt;
    s = subtype.rest;
    if (!take_key(s, "FRAMES:"))
        return std::nullopt;
    const auto frames = take_uint(s);
    if (!frames)
        return std::nullopt;
    out.frames = *frames;

    if (!row.v2)
        return out;
    if (!take_key(s, "PREGAP:"))
        return std::nullopt;
    const auto pregap = take_uint(s);
    if (!pregap)
        return std::nullopt;
    out.pregap = *pregap;
    if (!take_key(s, "PGTYPE:"))
        return std::nullopt;
    const Taken pgtype = take_token(out.pgtype, s);
    if (pgtype.written == 0)
        return std::nullopt;
    s = pgtype.rest;
    if (!take_key(s, "PGSUB:"))
        return std::nullopt;
    const Taken pgsub_tok = take_token(pgsub, s);
    if (pgsub_tok.written == 0)
        return std::nullopt;
    s = pgsub_tok.rest;
    if (!take_key(s, "POSTGAP:"))
        return std::nullopt;
    const auto postgap = take_uint(s);
    if (!postgap)
        return std::nullopt;
    out.postgap = *postgap;
    return out;
}

[[nodiscard]] static Ex<void> check_policy(const CuePolicy& policy, std::uint16_t site) {
    const CueAxis axis = unimplemented_axis(policy);
    if (axis != CueAxis::None) {
        return std::unexpected(Error{Errc::unimplemented, site, static_cast<std::uint32_t>(axis)});
    }
    return {};
}

DiscEngine::DiscEngine(
    CuePolicy p,
    ChdPrefetch& prefetch,
    infra::OptRef<const Vfs> vfs,
    infra::OptRef<const IImageOpener> opener
)
    : policy_(p),
      files_io_(vfs, opener),
      chd_(prefetch) {
    switch (p.frame_shaping) {
        case CuePolicy::FrameShaping::None:
            break;
        case CuePolicy::FrameShaping::DescrambleByHeader:
            frame_shaper_ = std::make_unique<SectorDescrambler>();
            break;
    }
    switch (p.pregap_model) {
        case CuePolicy::PregapModel::SingleAccumulator:
            cue_ = std::make_unique<SentinelCueProgram>(p);
            break;
        case CuePolicy::PregapModel::IndexTriple:
            cue_ = std::make_unique<IndexTripleCueProgram>();
            break;
        case CuePolicy::PregapModel::IndexPair:
            cue_ = std::make_unique<IndexPairCueProgram>();
            break;
    }
}

Ex<DiscEngine> DiscEngine::create(const Vfs& vfs, CuePolicy policy, ChdPrefetch& prefetch) {
    auto ok = check_policy(policy, ERR_SITE());
    if (!ok)
        return std::unexpected(ok.error());
    return DiscEngine(policy, prefetch, infra::OptRef<const Vfs>{vfs}, {});
}

Ex<DiscEngine> DiscEngine::create(
    const IImageOpener& opener,
    CuePolicy policy,
    ChdPrefetch& prefetch
) {
    auto ok = check_policy(policy, ERR_SITE());
    if (!ok)
        return std::unexpected(ok.error());
    return DiscEngine(policy, prefetch, {}, infra::OptRef<const IImageOpener>{opener});
}

Ex<void> DiscEngine::mount(std::string_view image_path) {
    if (image_path.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    if (detail::zip_split(image_path).zipped) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    (void)unmount();

    Ex<void> r = {};

    bool image_named_sub = false;
    if (ends_with_ci(image_path, ".cue")) {
        r = mount_cue(image_path);
        image_named_sub = true;
    } else if (ends_with_ci(image_path, ".chd")) {
        image_named_sub = true;
        r = mount_chd_path(image_path);
    } else {
        r = mount_iso(image_path);
    }

    if (!r) {
        (void)unmount();
        return r;
    }

    if (!chd_.attached() && toc_.last != 0) {
        const Track& t0 = toc_.tracks[0];
        table_.advise_first_window(t0);
    }

    if (policy_.sub_naming == CuePolicy::SubNaming::ImageSubOrCdg
        && !chd_.attached()
        && ends_with_ci(image_path, ".cue")) {
        open_sub_or_cdg_(image_path);
    }
    if (image_named_sub
        && policy_.sub_naming == CuePolicy::SubNaming::ImageFilename
        && image_path.size() > 4) {
        std::string sub_name(image_path.substr(0, image_path.size() - 4));
        sub_name.append(".sub");
        auto s = files_io_.open_file(sub_name);
        if (s) {
            subcode_.attach(std::move(*s));
            toc_.has_subcode = true;
        }
    }
    mounted_ = true;
    return {};
}

Ex<void> DiscEngine::unmount() {

    chd_.park();
    table_.reset();
    subcode_.reset();
    sub_shaper_.reset();
    chd_.release();
    toc_ = Toc{};
    mounted_ = false;
    return {};
}

Ex<void> DiscEngine::mount_cue(std::string_view cue_path) {
    return cue_->mount(cue_path, CueMount{files_io_, toc_, table_, subcode_, cdda_order_});
}

Ex<void> DiscEngine::mount_iso(std::string_view iso_path) {

    if (policy_.iso_sizing == CuePolicy::IsoSizing::Declared2048
        && !ends_with_ci(iso_path, ".iso")) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    cdda_order_ = CddaOrder::LittleEndian;

    auto f = files_io_.open_file(iso_path);
    if (!f)
        return std::unexpected(f.error());
    auto sz = (*f)->size();
    if (!sz)
        return std::unexpected(sz.error());

    if (policy_.iso_sizing == CuePolicy::IsoSizing::Declared2048) {
        return declare_iso_2048(std::move(*f), *sz);
    }

    struct Probe {
        std::uint32_t sector_size;
        bool mode2;
    };

    constexpr Probe kProbes[] = {
        {2048, false},
        {2352, false},
        {kMode2Cooked, true},
        {2352, true},
    };

    for (const Probe& p : kProbes) {

        std::uint64_t seek = 16ull * p.sector_size;
        if (p.sector_size == 2352 && !p.mode2)
            seek += 16;
        if (p.mode2)
            seek += 24;

        std::byte pvd[kUserDataBytes]{};
        auto rd = (*f)->read_at(seek, std::span<std::byte>(pvd, sizeof(pvd)));
        if (!rd || *rd != sizeof(pvd))
            continue;

        const auto b = infra::byte_cast<const unsigned char>(std::span(pvd));
        const bool iso9660 = b[0] == 1 && std::memcmp(b.data() + 1, "CD001", 5) == 0 && b[6] == 1;
        const bool high_sierra = b[8] == 1
            && std::memcmp(b.data() + 9, "CDROM", 5) == 0
            && b[14] == 1;
        if (!iso9660 && !high_sierra)
            continue;

        Track& t = toc_.tracks[0];
        t.number = TrackNumber{1};
        t.type = p.mode2 ? TrackType::Mode2 : TrackType::Mode1;
        t.sector_size = p.sector_size;
        t.start = Lba{0};
        t.end = Lba{static_cast<std::uint32_t>(sz->v / p.sector_size)};
        t.offset = 0;
        t.backing = 0;

        table_.hold_single(std::move(*f));

        toc_.last = 1;
        toc_.end = t.end;
        toc_.sector_size = p.sector_size;
        toc_.offset_unit = OffsetUnit::CueBytes;

        Track& lead_out = toc_.tracks[1];
        lead_out.number = TrackNumber{2};
        lead_out.type = TrackType::Cdda;
        lead_out.start = t.end;
        lead_out.end = t.end;
        return {};
    }
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

Ex<void> DiscEngine::declare_iso_2048(std::unique_ptr<IFile> f, FileSize sz) {
    constexpr std::uint32_t kDeclared = 2048;

    Track& t = toc_.tracks[0];
    t.number = TrackNumber{1};
    t.type = TrackType::Mode1;
    t.sector_size = kDeclared;
    t.start = Lba{0};
    t.end = Lba{static_cast<std::uint32_t>(sz.v / kDeclared)};
    t.offset = 0;
    t.backing = 0;

    table_.hold_single(std::move(f));

    toc_.last = 1;
    toc_.end = t.end;
    toc_.sector_size = kDeclared;
    toc_.offset_unit = OffsetUnit::CueBytes;

    Track& lead_out = toc_.tracks[1];
    lead_out.number = TrackNumber{2};
    lead_out.type = TrackType::Cdda;
    lead_out.start = t.end;
    lead_out.end = t.end;
    return {};
}

Ex<void> DiscEngine::mount_chd(
    std::unique_ptr<IChdSource> source,
    std::unique_ptr<IChdSource> prefetch_source
) {
    if (!source)
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    (void)unmount();

    auto attached = chd_.attach(std::move(source), std::move(prefetch_source));
    if (!attached)
        return attached;

    std::uint32_t sector_cnt = 0;
    std::uint32_t count = 0;
    for (; count < kMaxCueTracks; ++count) {
        const auto row = chd_.track_metadata(count);
        if (!row)
            break;
        const std::optional<ChdTrack> parsed = parse_chd_track(*row);
        if (!parsed)
            break;
        const ChdTrack& m = *parsed;

        Track& t = toc_.tracks[count];
        t.number = TrackNumber{static_cast<std::uint8_t>(count + 1)};

        const bool pregap_valid = (m.pgtype[0] == 'V');
        std::uint32_t pregap = m.pregap;
        if (count != 0) {
            Track& prev = toc_.tracks[count - 1];
            if (!pregap_valid)
                prev.end = Lba{prev.end.v + pregap};
            toc_.end = prev.end;
            t.start = toc_.end;
            if (pregap_valid)
                t.start = Lba{t.start.v + pregap};
        } else {
            t.start = Lba{pregap_valid ? pregap : 0u};
        }

        t.pregap = pregap;
        t.index_num = 2;
        if (!pregap_valid)
            pregap = 0;

        t.sector_size = 0;
        t.type = TrackType::Cdda;
        for (const ChdTypeRow& row_f : kChdTypes) {
            if (row_f.name == std::string_view(m.type)) {
                t.sector_size = row_f.sector_size;
                t.type = row_f.type;
                break;
            }
        }

        const std::string_view subtype(m.subtype);
        if (subtype == "RW" || subtype == "RW_RAW") {

            if (!toc_.has_subcode && subtype == "RW") {
                sub_shaper_ = std::make_unique<RwInterleaver>();
            }
            toc_.has_subcode = true;
        }

        t.offset = static_cast<std::int64_t>(sector_cnt)
            + static_cast<std::int64_t>(pregap)
            - static_cast<std::int64_t>(t.start.v);
        t.end = Lba{t.start.v + m.frames - pregap};
        toc_.end = Lba{t.end.v + m.postgap};
        sector_cnt += ((m.frames + static_cast<std::uint32_t>(kChdTrackPadding) - 1)
                       / static_cast<std::uint32_t>(kChdTrackPadding))
            * static_cast<std::uint32_t>(kChdTrackPadding);
    }

    if (count == 0) {
        (void)unmount();
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }

    toc_.last = static_cast<std::uint8_t>(count);
    toc_.sector_size = toc_.tracks[0].sector_size;

    cdda_order_ = CddaOrder::BigEndian;

    toc_.offset_unit = OffsetUnit::ChdSectors;

    if (policy_.post_load_shift == CuePolicy::PostLoadTocShift::FakeLeadInPregap150) {
        const auto lead_in = static_cast<std::uint32_t>(kRedBookPregap);
        for (std::uint32_t i = 0; i < count; ++i) {
            Track& t = toc_.tracks[i];
            if (i == 0) {

                t.pregap = lead_in;
                t.start = Lba{lead_in};
                t.end = Lba{t.end.v + lead_in};
            } else {

                const std::uint32_t frame_cnt = (t.end.v - t.start.v) + t.pregap;
                t.start = toc_.tracks[i - 1].end;
                t.end = Lba{t.start.v + frame_cnt};
            }
            t.offset -= static_cast<std::int64_t>(lead_in);
        }
        toc_.end = toc_.tracks[count - 1].end;
    }

    if (policy_.post_load_shift == CuePolicy::PostLoadTocShift::Uniform150) {
        const auto lead_in = static_cast<std::uint32_t>(kRedBookPregap);
        for (std::uint32_t i = 0; i < count; ++i) {
            Track& t = toc_.tracks[i];
            t.start = Lba{t.start.v + lead_in};
            t.end = Lba{t.end.v + lead_in};
            t.offset -= static_cast<std::int64_t>(lead_in);
        }
        toc_.end = Lba{toc_.end.v + lead_in};
    }

    Track& lead_out = toc_.tracks[count];
    lead_out.number = TrackNumber{static_cast<std::uint8_t>(count + 1)};
    lead_out.type = TrackType::Cdda;
    lead_out.start = toc_.end;
    lead_out.end = toc_.end;

    mounted_ = true;
    return {};
}

std::optional<TrackIndex> DiscEngine::track_for_lba(Lba lba) const {
    return svc::track_for_lba(toc_, lba);
}

std::optional<TrackIndex> DiscEngine::source_track(Lba lba) const {
    const auto ti = track_for_lba(lba);
    if (!ti)
        return std::nullopt;

    if (chd_.attached())
        return ti;

    if (policy_.pregap_model == CuePolicy::PregapModel::IndexPair)
        return ti;
    const Track& t = toc_.tracks[ti->v];

    if (lba < t.start && t.number.v > 1) {
        return TrackIndex{static_cast<std::uint8_t>(t.number.v - 2)};
    }
    return ti;
}

Ex<std::size_t> DiscEngine::read_form(Lba lba, std::span<std::byte> dst, Form form) {
    if (!mounted_)
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});

    const auto ti = source_track(lba);
    if (!ti)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), lba.v});
    const Track& t = toc_.tracks[ti->v];

    const std::uint32_t ss = t.sector_size;
    if (ss == 0) {

        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), ti->v});
    }

    std::size_t out_len = 0;
    std::size_t dst_off = 0;
    std::uint32_t s_off = 0;
    std::size_t len = 0;

    switch (form) {
        case Form::Native:
            out_len = ss;
            len = ss;
            break;
        case Form::UserData:

            out_len = kUserDataBytes;
            len = kUserDataBytes;
            if (ss != kUserDataBytes)
                s_off = (t.type == TrackType::Mode2) ? 24u : 16u;
            if (static_cast<std::size_t>(s_off) + len > ss) {
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), ss});
            }
            break;
        case Form::RedBook:

            out_len = kCdDataSize;
            if (ss == kCdDataSize) {
                len = kCdDataSize;
            } else if (ss == kMode2Cooked) {
                dst_off = 16;
                len = kMode2Cooked;
            } else if (ss == kUserDataBytes) {
                dst_off = 16;
                len = kUserDataBytes;
            } else {
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), ss});
            }
            break;
    }

    if (dst.size() < out_len) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(out_len)}
        );
    }

    if (form == Form::RedBook && ss != kCdDataSize) {

        std::memset(dst.data(), 0, kCdDataSize);
        if (ss == kUserDataBytes) {

            auto b = infra::byte_cast<unsigned char>(dst);
            b[0] = 0x00;
            std::memset(b.data() + 1, 0xff, 10);
            b[11] = 0x00;
            const std::uint32_t f_lba = lba.v + static_cast<std::uint32_t>(kRedBookPregap);
            b[12] = bcd(f_lba / (kFramesPerSecond * 60));
            b[13] = bcd((f_lba / kFramesPerSecond) % 60);
            b[14] = bcd(f_lba % kFramesPerSecond);
            b[15] = 0x01;
        }
    }

    IDiscBacking& backing = chd_.attached() ? static_cast<IDiscBacking&>(chd_) : table_;
    auto rd = backing.read_payload(lba, t, s_off, dst.subspan(dst_off, len));
    if (!rd)
        return std::unexpected(rd.error());
    normalize_cdda_(t, form, dst.subspan(dst_off, len));
    return out_len;
}

Ex<std::size_t> DiscEngine::read_sector(Lba lba, std::span<std::byte> dst) {
    return read_form(lba, dst, Form::Native);
}

Ex<std::size_t> DiscEngine::read_user_data(Lba lba, std::span<std::byte> dst) {
    return read_form(lba, dst, Form::UserData);
}

void DiscEngine::normalize_cdda_(
    const Track& t,
    Form form,
    std::span<std::byte> frame
) const noexcept {

    if (form != Form::RedBook)
        return;
    if (t.type != TrackType::Cdda)
        return;
    if (cdda_order_ == kWireCddaOrder)
        return;
    for (std::size_t i = 0; i + 1 < frame.size(); i += 2) {
        const std::byte lo = frame[i];
        frame[i] = frame[i + 1];
        frame[i + 1] = lo;
    }
}

Ex<std::size_t> DiscEngine::read_raw_frame(Lba lba, std::span<std::byte> dst) {
    return read_form(lba, dst, Form::RedBook);
}

Ex<std::size_t> DiscEngine::read_subcode(Lba lba, std::span<std::byte> dst) {
    if (!mounted_)
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    if (dst.size() < kSubcodeBytes) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(dst.size())}
        );
    }

    if (subcode_.attached())
        return subcode_.read(lba, dst);

    if (chd_.attached()) {
        if (!toc_.has_subcode)
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

        const auto ti = source_track(lba);
        if (!ti)
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), lba.v});
        return chd_.read_subcode(lba, toc_.tracks[ti->v], dst);
    }

    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

Ex<std::size_t> DiscEngine::read_full_frame(Lba lba, std::span<std::byte> dst) {
    if (dst.size() < kCdFrameSize) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(dst.size())}
        );
    }
    const std::span<std::byte, kCdFrameSize> frame = dst.first<kCdFrameSize>();
    auto r = read_raw_frame(lba, frame.first<kCdDataSize>());
    if (!r)
        return std::unexpected(r.error());
    const std::span<std::byte> tail = frame.subspan(kCdDataSize, kSubcodeBytes);
    std::memset(tail.data(), 0, tail.size());

    if (toc_.has_subcode) {
        if (!read_subcode(lba, tail)) {
            std::memset(tail.data(), 0, tail.size());
        } else if (sub_shaper_ != nullptr) {
            sub_shaper_->shape(lba, frame);
        }
    }
    if (frame_shaper_ != nullptr)
        frame_shaper_->shape(lba, frame);
    return kCdFrameSize;
}

void DiscEngine::open_sub_or_cdg_(std::string_view cue_path) {
    const SubcodeSidecar::Found found = subcode_.open_sub_or_cdg(cue_path, files_io_);
    if (found == SubcodeSidecar::Found::None)
        return;
    toc_.has_subcode = true;
    if (found == SubcodeSidecar::Found::Sub)
        sub_shaper_ = std::make_unique<RwInterleaver>();
}

std::uint32_t DiscEngine::seek_ms(Lba from_lba, Lba to_lba) const {
    return svc::seek_ms(from_lba, to_lba);
}

void DiscEngine::advise_ahead(Lba lba) noexcept {

    if (chd_.attached() || !mounted_)
        return;
    const auto ti = source_track(lba);
    if (!ti)
        return;
    const Track& t = toc_.tracks[ti->v];
    if (t.sector_size == 0)
        return;
    table_.advise_ahead(lba, t, kAdviseAheadSectors);
}

void DiscEngine::prefetch_hint(Lba lba) {

    if (chd_.prefetch_off())
        return;

    const auto ti = source_track(lba);
    if (!ti)
        return;
    chd_.prefetch_hint(lba, toc_.tracks[ti->v]);
}

Ex<void> DiscEngine::mount_chd_path(std::string_view chd_path) {
    auto f = files_io_.open_file(chd_path);
    if (!f)
        return std::unexpected(f.error());

    IFile* const primary_raw = f->get();
    auto src = open_chd(std::move(*f));
    if (!src)
        return std::unexpected(src.error());

    std::unique_ptr<IChdSource> second;
    if (chd_.prefetch_opened()) {
        if (auto f2 = files_io_.open_file(chd_path)) {
            if (auto s2 = open_chd(std::move(*f2)); s2)
                second = std::move(*s2);
        }
    }
    auto mounted = mount_chd(std::move(*src), std::move(second));
    if (!mounted)
        return mounted;

    chd_.advise_first_window(*primary_raw);
    return {};
}

}
