// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disc_engine.h"

#include "svc/chd_prefetch.h"
#include "svc/chd_track.h"
#include "svc/data_sector_sealer.h"
#include "svc/index_pair_cue_program.h"
#include "svc/index_triple_cue_program.h"
#include "svc/rw_interleaver.h"
#include "svc/sector_descrambler.h"
#include "svc/sentinel_cue_program.h"
#include "svc/subcode_interleaver.h"

#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "infra/endian.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "disc_lex.h"
#include "svc/asset_name.h"

namespace mister::svc {

using namespace disc::detail;

namespace {

using proto::BlockCount;

constexpr std::string_view kIso9660Magic = "CD001";
constexpr std::string_view kHighSierraMagic = "CDROM";

[[nodiscard]] bool has_descriptor(
    std::span<const unsigned char> b,
    std::string_view magic
) noexcept {
    return b[0] == 1
        && std::memcmp(b.data() + 1, magic.data(), magic.size()) == 0
        && b[1 + magic.size()] == 1;
}

}

[[nodiscard]] static Ex<void> check_policy(const CuePolicy& policy, infra::ErrorSite site) {
    const CueAxis axis = unimplemented_axis(policy);
    if (axis != CueAxis::None) {
        return std::unexpected(Error{Errc::unimplemented, site, static_cast<std::uint32_t>(axis)});
    }
    return {};
}

DiscEngine::DiscEngine(CuePolicy p, ChdPrefetch& prefetch, DiscFileAccess files_io)
    : policy_(p),
      files_io_(std::move(files_io)),
      chd_(prefetch) {
    switch (p.frame_shaping) {
        case CuePolicy::FrameShaping::None:
            break;
        case CuePolicy::FrameShaping::DescrambleByHeader:
            frame_shaper_ = std::make_unique<SectorDescrambler>();
            break;
        case CuePolicy::FrameShaping::SealDataSector:
            sector_sealer_.emplace();
            break;
    }
    switch (p.subcode_shaping) {
        case CuePolicy::SubcodeShaping::None:
            break;
        case CuePolicy::SubcodeShaping::Interleave:
            block_shaper_.emplace();
            break;
    }
    switch (p.program) {
        case CuePolicy::Program::SentinelExclusive:
        case CuePolicy::Program::SentinelPregapRelative:
            cue_ = std::make_unique<SentinelCueProgram>(p);
            break;
        case CuePolicy::Program::IndexTriple:
            cue_ = std::make_unique<IndexTripleCueProgram>();
            break;
        case CuePolicy::Program::IndexPair:
            cue_ = std::make_unique<IndexPairCueProgram>();
            break;
    }
}

Ex<DiscEngine> DiscEngine::create(const Vfs& vfs, CuePolicy policy, ChdPrefetch& prefetch) {
    auto ok = check_policy(policy, ERR_SITE());
    if (!ok)
        return std::unexpected(ok.error());
    return DiscEngine(policy, prefetch, DiscFileAccess{vfs});
}

Ex<DiscEngine> DiscEngine::create(
    const IImageOpener& opener,
    CuePolicy policy,
    ChdPrefetch& prefetch
) {
    auto ok = check_policy(policy, ERR_SITE());
    if (!ok)
        return std::unexpected(ok.error());
    return DiscEngine(policy, prefetch, DiscFileAccess{opener});
}

Ex<void> DiscEngine::mount(std::string_view image_path) {
    if (image_path.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    if (asset_name::zip_split(image_path).zipped) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    (void)unmount();

    Ex<void> r = {};

    bool image_named_sub = false;
    if (infra::iends_with(image_path, ".cue")) {
        r = mount_cue(image_path);
        image_named_sub = true;
    } else if (infra::iends_with(image_path, ".chd")) {
        image_named_sub = true;
        r = mount_chd_path(image_path);
    } else {
        r = mount_iso(image_path);
    }

    if (!r) {
        (void)unmount();
        return r;
    }

    if (!chd_.attached() && toc_.last != TrackCount{}) {
        const Track& t0 = toc_.at(TrackIndex{});
        table_.advise_first_window(t0);
    }

    if (policy_.sub_naming == CuePolicy::SubNaming::ImageSubOrCdg
        && !chd_.attached()
        && infra::iends_with(image_path, ".cue")) {
        open_sub_or_cdg_(image_path);
    }
    if (image_named_sub
        && policy_.sub_naming == CuePolicy::SubNaming::ImageFilename
        && image_path.size() > kExtSub.size()) {
        std::string sub_name(image_path.substr(0, image_path.size() - kExtSub.size()));
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
        && !infra::iends_with(iso_path, ".iso")) {
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
        {kUserDataBytes, false},
        {kRawFrameBytes, false},
        {kMode2Cooked, true},
        {kRawFrameBytes, true},
    };

    for (const Probe& p : kProbes) {

        constexpr std::uint64_t kFirstDescriptorSector = 16;
        constexpr std::size_t kRawHeaderMode1 = 16;
        constexpr std::size_t kRawHeaderMode2 = 24;
        std::uint64_t seek = kFirstDescriptorSector * p.sector_size;
        if (p.sector_size == kRawFrameBytes && !p.mode2)
            seek += kRawHeaderMode1;
        if (p.mode2)
            seek += kRawHeaderMode2;

        std::byte pvd[kUserDataBytes]{};
        auto rd = (*f)->read_at(seek, std::span<std::byte>(pvd, sizeof(pvd)));
        if (!rd || *rd != sizeof(pvd))
            continue;

        constexpr std::size_t kHighSierraOffset = 8;
        const auto b = infra::byte_cast<const unsigned char>(std::span(pvd));
        if (!has_descriptor(b, kIso9660Magic)
            && !has_descriptor(b.subspan(kHighSierraOffset), kHighSierraMagic)) {
            continue;
        }

        hold_single_track_(
            p.mode2 ? TrackType::Mode2 : TrackType::Mode1,
            proto::BlockSize{p.sector_size},
            *sz,
            std::move(*f)
        );
        return {};
    }
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

void DiscEngine::hold_single_track_(
    TrackType type,
    proto::BlockSize sector_size,
    FileSize sz,
    std::unique_ptr<IFile> f
) {

    const std::uint32_t sector_bytes = sector_size.v;
    Track& t = toc_.at(TrackIndex{});
    t.number = TrackNumber{1};
    t.type = type;
    t.sector_size = sector_bytes;
    t.start = Lba{0};
    t.end = proto::end_lba(sz, sector_size);
    t.offset = 0;
    t.backing = OpenFileId{0};

    table_.hold_single(std::move(f));

    toc_.end = t.end;
    toc_.sector_size = sector_bytes;
    toc_.close(TrackCount{1});
}

Ex<void> DiscEngine::declare_iso_2048(std::unique_ptr<IFile> f, FileSize sz) {
    hold_single_track_(TrackType::Mode1, proto::BlockSize{kUserDataBytes}, sz, std::move(f));
    return {};
}

Ex<void> DiscEngine::mount_chd(
    std::unique_ptr<IChdSource> source,
    std::unique_ptr<IChdSource> prefetch_source
) {
    if (!source)
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    (void)unmount();

    auto attached = chd_.attach(
        ChdHandles{.primary = std::move(source), .prefetch = std::move(prefetch_source)}
    );
    if (!attached)
        return attached;

    BlockCount sector_cnt{0};
    TrackCount count{};
    for (; count < kMaxCueTracks; count += TrackCount{1}) {
        const auto row = chd_.track_metadata(count.next_index());
        if (!row)
            break;
        const std::optional<ChdTrack> parsed = parse_chd_track(*row);
        if (!parsed)
            break;
        const ChdTrack& m = *parsed;

        Track& t = toc_.at(count.next_index());
        t.number = count.next_index().number();

        BlockCount pregap = m.pregap;
        if (!count.empty()) {
            Track& prev = toc_.at(count.last_index());
            if (!m.pregap_held)
                prev.end += pregap;
            toc_.end = prev.end;
            t.start = toc_.end;
            if (m.pregap_held)
                t.start += pregap;
        } else {
            t.start = Lba{0} + (m.pregap_held ? pregap : BlockCount{0});
        }

        t.pregap = pregap;
        t.index_num = Track::kIndexes;
        if (!m.pregap_held)
            pregap = BlockCount{0};

        m.apply_type_to(t);

        if (m.subcode != ChdSubcode::None) {

            if (!toc_.has_subcode && m.subcode == ChdSubcode::Rw) {
                sub_shaper_ = std::make_unique<RwInterleaver>();
            }
            toc_.has_subcode = true;
        }

        t.offset = std::int64_t{sector_cnt.v} + std::int64_t{pregap.v} - std::int64_t{t.start.v};
        t.end = t.start + m.frames - pregap;
        toc_.end = t.end + m.postgap;

        sector_cnt += m.frames.rounded_up_to(kChdTrackPadding);
    }

    if (count.empty()) {
        (void)unmount();
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }

    toc_.sector_size = toc_.at(TrackIndex{}).sector_size;

    cdda_order_ = CddaOrder::BigEndian;

    cue_->shift_chd_toc(toc_, count);

    toc_.close(count);

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

    if (cue_->pregap_in_own_file())
        return ti;
    const Track& t = toc_.at(*ti);

    if (lba < t.start && t.number > TrackNumber{1}) {
        return t.number.index() - TrackCount{1};
    }
    return ti;
}

Ex<std::span<std::byte>> DiscEngine::read_form(
    Lba lba,
    std::span<std::byte> dst,
    Form form,
    std::optional<SectorHeader> header
) {
    if (!mounted_)
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});

    const auto ti = source_track(lba);
    if (!ti)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), lba.v});
    const Track& t = toc_.at(*ti);

    const std::uint32_t ss = t.sector_size;
    if (ss == 0) {

        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), ti->v});
    }
    const std::optional<FormShape> shape = shape_of(form, ss, t.type);
    if (!shape) {

        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), ss});
    }
    const FormShape& f = *shape;

    if (dst.size() < f.out_len) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(f.out_len)}
        );
    }

    if (form == Form::RedBook && ss != kRawFrameBytes) {

        std::memset(dst.data(), 0, kRawFrameBytes);
    }

    IDiscBacking& backing = chd_.attached() ? static_cast<IDiscBacking&>(chd_) : table_;
    auto rd = backing.read_payload(lba, t, f.s_off, dst.subspan(f.dst_off, f.len));
    if (!rd)
        return std::unexpected(rd.error());
    normalize_cdda_(t, form, dst.subspan(f.dst_off, f.len));

    if (form == Form::RedBook && t.type != TrackType::Cdda) {
        if (header) {
            header->write(dst.first<SectorHeader::kBytes>());
        } else if (ss == kUserDataBytes) {

            SectorHeader::mode1_at(Msf::of_lba_bcd_masked(lba + kRedBookPregap))
                .write(dst.first<SectorHeader::kBytes>());
        }
    }

    if (sector_sealer_ && form == Form::RedBook && t.type != TrackType::Cdda)
        sector_sealer_->seal(std::span<std::byte, kRawFrameBytes>(dst.data(), kRawFrameBytes));
    return dst.first(f.out_len);
}

std::optional<DiscEngine::FormShape> DiscEngine::shape_of(
    Form form,
    std::uint32_t ss,
    TrackType type
) noexcept {
    constexpr std::uint32_t kRawHeaderMode1 = 16;
    constexpr std::uint32_t kRawHeaderMode2 = 24;
    if (ss == 0)
        return std::nullopt;
    FormShape out{};
    switch (form) {
        case Form::Native:
            out.out_len = ss;
            out.len = ss;
            return out;
        case Form::UserData:

            out.out_len = kUserDataBytes;
            out.len = kUserDataBytes;
            if (ss != kUserDataBytes)
                out.s_off = (type == TrackType::Mode2) ? kRawHeaderMode2 : kRawHeaderMode1;
            if (out.s_off + out.len > ss)
                return std::nullopt;
            return out;
        case Form::RedBook:

            out.out_len = kRawFrameBytes;
            if (ss == kRawFrameBytes) {
                out.len = kRawFrameBytes;
            } else if (ss == kMode2Cooked) {
                out.dst_off = kRawHeaderMode1;
                out.len = kMode2Cooked;
            } else if (ss == kUserDataBytes) {
                out.dst_off = kRawHeaderMode1;
                out.len = kUserDataBytes;
            } else {
                return std::nullopt;
            }
            return out;
    }
    return std::nullopt;
}

Ex<std::span<std::byte>> DiscEngine::read_sector(Lba lba, std::span<std::byte> dst) {
    return read_form(lba, dst, Form::Native);
}

Ex<std::span<std::byte>> DiscEngine::read_user_data(Lba lba, std::span<std::byte> dst) {
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
    constexpr std::size_t kCddaSampleBytes = 2;
    for (std::size_t i = 0; i + kCddaSampleBytes <= frame.size(); i += kCddaSampleBytes) {
        std::ranges::reverse(frame.subspan(i, kCddaSampleBytes));
    }
}

Ex<std::span<std::byte>> DiscEngine::read_raw_frame(
    Lba lba,
    std::span<std::byte> dst,
    std::optional<SectorHeader> header
) {
    return read_form(lba, dst, Form::RedBook, header);
}

Ex<std::span<std::byte>> DiscEngine::read_subcode(Lba lba, std::span<std::byte> dst) {
    if (!mounted_)
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    if (dst.size() < SubcodeBlock::extent) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(dst.size())}
        );
    }

    Ex<std::span<std::byte>> got = std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (subcode_.attached()) {
        got = subcode_.read(lba, dst);
    } else if (chd_.attached()) {
        if (!toc_.has_subcode)
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

        const auto ti = source_track(lba);
        if (!ti)
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), lba.v});
        got = chd_.read_subcode(lba, toc_.at(*ti), dst);
    }

    if (!got)
        return got;
    if (block_shaper_)
        block_shaper_->shape(dst.first<SubcodeBlock::extent>());
    return dst.first<SubcodeBlock::extent>();
}

Ex<std::span<std::byte>> DiscEngine::read_full_frame(Lba lba, std::span<std::byte> dst) {
    if (dst.size() < kFullFrameBytes) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(dst.size())}
        );
    }
    const std::span<std::byte, kFullFrameBytes> frame = dst.first<kFullFrameBytes>();
    auto r = read_raw_frame(lba, frame.first<kRawFrameBytes>());
    if (!r)
        return std::unexpected(r.error());
    const std::span<std::byte> tail = frame.subspan(kRawFrameBytes, kSubcodeBytes);
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
    return frame;
}

void DiscEngine::open_sub_or_cdg_(std::string_view cue_path) {
    const SubcodeSidecar::Found found = subcode_.open_sub_or_cdg(cue_path, files_io_);
    if (found == SubcodeSidecar::Found::None)
        return;
    toc_.has_subcode = true;
    if (found == SubcodeSidecar::Found::Sub)
        sub_shaper_ = std::make_unique<RwInterleaver>();
}

SeekTime DiscEngine::seek_time(Lba from_lba, Lba to_lba) const {
    return svc::seek_time(from_lba, to_lba);
}

void DiscEngine::advise_ahead(Lba lba) noexcept {

    if (chd_.attached() || !mounted_)
        return;
    const auto ti = source_track(lba);
    if (!ti)
        return;
    const Track& t = toc_.at(*ti);
    if (t.sector_size == 0)
        return;
    table_.advise_ahead(lba, t, kAdviseAheadSectors);
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
