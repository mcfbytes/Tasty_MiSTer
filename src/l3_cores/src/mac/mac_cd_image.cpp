// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mac/mac_cd_image.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "cores/mac/image_ext.h"
#include "cores/mac/mac_windows.h"
#include "infra/byte_view.h"

namespace mister::cores::mac {

namespace {

constexpr std::size_t kFrameBytes = svc::disc::kRawFrameBytes;

constexpr proto::BlockSize kSector{svc::disc::kUserDataBytes};
constexpr proto::BlockSize kRawSector{svc::disc::kRawFrameBytes};
constexpr proto::BlockSize kMode2Cooked{2336};

constexpr proto::FileSize kCookedSubheader{8};
constexpr proto::FileSize kMode1Header{16};
constexpr proto::FileSize kMode2Header{24};
constexpr proto::BlockCount kOneBlock{1};

constexpr svc::disc::CuePolicy kCuePolicy{
    .type_detect = svc::disc::CuePolicy::TypeDetect::PerTrack,
    .offset_op = svc::disc::CuePolicy::OffsetOp::Assign,
    .sub_naming = svc::disc::CuePolicy::SubNaming::None,
    .program = svc::disc::CuePolicy::Program::SentinelExclusive,
};

[[nodiscard]] std::unexpected<Error> rejected() noexcept {
    return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
}

[[nodiscard]] bool volume_descriptor(std::span<const std::byte> at) noexcept {
    constexpr std::string_view kIso{"\x01"
                                    "CD001"};
    constexpr std::string_view kHighSierra{"\x01"
                                           "CDROM"};
    const auto text = infra::byte_cast<const char>(at);
    const std::string_view seen(text.data(), text.size());
    return seen.starts_with(kIso) || seen.starts_with(kHighSierra);
}

}

constexpr proto::FileSize MacCdImage::user_at_(bool mode2, proto::BlockSize sector) noexcept {
    if (sector == kSector)
        return {};
    if (sector == kMode2Cooked)
        return kCookedSubheader;
    return mode2 ? kMode2Header : kMode1Header;
}

void MacCdImage::publish_() noexcept {
    if (reads_cell_)
        reads_cell_->publish(reads_);
}

void MacCdImage::close_() noexcept {
    file_.reset();
    if (engine_ && engine_->mounted())
        (void)engine_->unmount().has_value();
    served_ = Served::Nothing;
    data_track_.reset();
    sectors_ = {};
    toc_block_.fill(std::byte{0});
}

void MacCdImage::release(proto::SlotIndex) noexcept {
    TASTY_SEAT_BODY(MacCdImage);
    close_();
}

Ex<infra::ByteCount> MacCdImage::write_at(proto::FileOffset, std::span<const std::byte>) {
    TASTY_SEAT_BODY(MacCdImage);
    return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
}

Ex<proto::FileSize> MacCdImage::open_extent(
    proto::SlotIndex,
    const svc::BlockCtl::StorageOrder& order
) {
    TASTY_SEAT_BODY(MacCdImage);
    close_();
    const auto text = infra::byte_cast<const char>(order.view());
    const std::string path(text.data(), text.size());
    const Ex<proto::FileSize> opened = kChdExt.names(path) || kCueExt.names(path) ? open_disc_(path)
                                                                                  : open_raw_(path);
    if (!opened)
        close_();
    reads_.mount = reads_.mount.next();
    reads_.data_reads = {};
    reads_.translated = served_ == Served::Raw || served_ == Served::Disc;
    publish_();
    return opened;
}

proto::FileSize MacCdImage::settle_(const svc::disc::Toc& toc) {
    const std::optional<svc::TrackIndex> data = first_data_track(toc);
    toc_block_ = mcda_toc(toc, data);
    if (data) {
        data_track_ = toc.tracks[*data];
        sectors_ = data_track_->end - data_track_->start;
    } else {

        data_track_.reset();
        sectors_ = toc.end - proto::Lba{};
    }
    return kSector.bytes_of(sectors_);
}

Ex<proto::FileSize> MacCdImage::open_disc_(std::string_view path) {
    if (!engine_) {
        auto built = svc::disc::DiscEngine::create(vfs_, kCuePolicy, prefetch_);
        if (!built)
            return std::unexpected(built.error());
        engine_.emplace(std::move(*built));
    }
    if (auto mounted = engine_->mount(path); !mounted)
        return rejected();
    if (engine_->toc().last.empty())
        return rejected();
    served_ = Served::Disc;
    return settle_(engine_->toc());
}

std::optional<MacCdImage::Layout> MacCdImage::probe_(svc::IFile& file) const {
    constexpr Layout kFlat{.sector = kSector, .user_at = {}, .mode2 = false};
    constexpr Layout kMode1Raw{.sector = kRawSector, .user_at = kMode1Header, .mode2 = false};
    constexpr std::array<Layout, 4> kCandidates{{
        kFlat,
        kMode1Raw,
        {.sector = kMode2Cooked, .user_at = kCookedSubheader, .mode2 = true},
        {.sector = kRawSector, .user_at = kMode2Header, .mode2 = true},
    }};
    constexpr proto::Lba kDescriptorSector{16};
    constexpr std::size_t kDescriptorBytes = 6;
    std::array<std::byte, kDescriptorBytes> seen{};
    for (const Layout& c : kCandidates) {
        const proto::FileOffset at = c.sector.position_of(kDescriptorSector) + c.user_at;
        if (file.read_exact_at(at, seen) && volume_descriptor(seen))
            return c;
    }

    constexpr std::byte kFill{0xFF};
    constexpr std::array<std::byte, 12> kSync{
        std::byte{},
        kFill,
        kFill,
        kFill,
        kFill,
        kFill,
        kFill,
        kFill,
        kFill,
        kFill,
        kFill,
        std::byte{}
    };
    std::array<std::byte, kSync.size()> head{};
    if (file.read_exact_at(proto::kFileStart, head) && head == kSync)
        return kMode1Raw;
    return std::nullopt;
}

Ex<proto::FileSize> MacCdImage::open_raw_(std::string_view path) {
    auto file = vfs_.open(path, svc::OpenMode::Read);
    if (!file)
        return rejected();
    const auto size = (*file)->size();
    if (!size)
        return rejected();
    const std::optional<Layout> found = probe_(**file);
    file_ = std::move(*file);
    if (!found || found->sector == kSector) {
        served_ = Served::Flat;
        return *size;
    }
    layout_ = *found;
    served_ = Served::Raw;
    svc::disc::Toc toc{};
    svc::disc::Track& t = toc.tracks[svc::TrackIndex{}];
    t.number = svc::TrackIndex{}.number();
    t.type = layout_.mode2 ? svc::disc::TrackType::Mode2 : svc::disc::TrackType::Mode1;
    t.sector_size = layout_.sector.block_bytes();
    t.start = proto::Lba{};
    t.end = proto::end_lba(*size, layout_.sector);
    toc.end = t.end;
    toc.sector_size = layout_.sector.block_bytes();
    toc.close(svc::TrackCount{1});
    return settle_(toc);
}

void MacCdImage::read_sector_part_(
    proto::Lba sector,
    proto::FileSize from,
    std::span<std::byte> dst
) {
    if (!data_track_ || !sectors_.holds(sector, kOneBlock))
        return;
    if (served_ == Served::Raw) {
        const proto::FileOffset at = layout_.sector.position_of(sector) + layout_.user_at + from;
        if (!file_->read_exact_at(at, dst))
            std::memset(dst.data(), 0, dst.size());
        return;
    }
    const svc::disc::Track& t = *data_track_;
    const auto got = engine_->read_sector(t.start + (sector - proto::Lba{}), scratch_);
    const proto::FileSize user = user_at_(
        t.type == svc::disc::TrackType::Mode2,
        proto::BlockSize{t.sector_size}
    );
    const auto begin = (user + from).file_word<std::size_t>();
    if (!got || begin + dst.size() > got->size())
        return;
    std::memcpy(dst.data(), got->data() + begin, dst.size());
}

void MacCdImage::read_data_(proto::FileOffset at, std::span<std::byte> dst) {
    if (served_ == Served::Flat) {
        (void)file_->read_prefix_at(at, dst);
        return;
    }
    const proto::FileSize one_sector = kSector.bytes_of(kOneBlock);
    std::size_t done = 0;
    while (done < dst.size()) {
        const proto::FileSize pos = (at - proto::kFileStart) + proto::FileSize{done};
        const proto::FileSize from = pos.bytes_past_whole(one_sector);
        const std::size_t n = std::min(
            (one_sector - from).file_word<std::size_t>(),
            dst.size() - done
        );
        read_sector_part_(proto::Lba{} + pos.whole_units(kSector), from, dst.subspan(done, n));
        done += n;
    }
}

void MacCdImage::read_audio_(proto::Lba frame, std::span<std::byte> dst) {
    if (served_ != Served::Disc)
        return;
    for (std::size_t done = 0; done + kFrameBytes <= dst.size();
         done += kFrameBytes, frame += kOneBlock) {
        const auto track = engine_->track_for_lba(frame);
        if (!track || engine_->toc().tracks[*track].type != svc::disc::TrackType::Cdda)
            continue;
        const std::span<std::byte> out = dst.subspan(done, kFrameBytes);
        if (!engine_->read_raw_frame(frame, out))
            std::memset(out.data(), 0, out.size());
    }
}

Ex<infra::ByteCount> MacCdImage::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(MacCdImage);
    std::memset(dst.data(), 0, dst.size());
    if (served_ == Served::Nothing)
        return infra::ByteCount{dst.size()};
    if (kCdAudio.holds_byte(at)) {

        read_audio_(proto::Lba{} + (at - kCdAudio.first_byte()).whole_units(kRawSector), dst);
    } else if (kCdToc.holds_byte(at)) {
        if (served_ != Served::Flat) {
            const std::size_t from = (at - kCdToc.first_byte()).file_word<std::size_t>();
            const std::size_t n = std::min(toc_block_.size() - from, dst.size());
            std::memcpy(dst.data(), toc_block_.data() + from, n);
        }
    } else if (kCdData.holds_byte(at)) {
        reads_.data_reads = reads_.data_reads.next();
        publish_();
        read_data_(at, dst);
    }
    return infra::ByteCount{dst.size()};
}

}
