// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_core.h"
#include "cores/core_adopt.h"
#include "hal/selected.h"

#include <algorithm>
#include <cstring>

#include "cores/cdwire.h"
#include "cores/mounted_path.h"
#include "cores/psx_disc_identity.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "proto/image_bracket.h"
#include "proto/image_sink.h"
#include "svc/toc_frame.h"

namespace mister::cores {
namespace {

constexpr proto::Lba kRootFolderLba{150 + 22};
constexpr proto::Lba kLicenseLba{154};

constexpr proto::BlockCount kRootDirSectors{25};

inline constexpr std::string_view
    kLicenceText = "          Licensed  by          Sony Computer Entertainment ";

constexpr std::size_t kLicenceOffset = kLicenceText.size();
static_assert(kLicenceOffset == 60);

struct PrefixRegion {
    const char* prefix;
    svc::DiscRegion region;
};

constexpr PrefixRegion kPrefixTable[] = {
    {"SCES", svc::DiscRegion::Europe},
    {"SLES", svc::DiscRegion::Europe},
    {"SCUS", svc::DiscRegion::Usa},
    {"SLUS", svc::DiscRegion::Usa},
    {"SCPM", svc::DiscRegion::Japan},
    {"SLPM", svc::DiscRegion::Japan},
    {"SCPS", svc::DiscRegion::Japan},
    {"SLPS", svc::DiscRegion::Japan},
    {"SIPS", svc::DiscRegion::Japan},
    {"PUPX", svc::DiscRegion::Usa},
    {"PEPX", svc::DiscRegion::Europe},
    {"PAPX", svc::DiscRegion::Japan},
    {"PCPX", svc::DiscRegion::Japan},
    {"SCZS", svc::DiscRegion::Japan},
    {"SCED", svc::DiscRegion::Europe},
    {"SLED", svc::DiscRegion::Europe},
};

std::span<const std::uint8_t> find_bytes(
    std::span<const std::uint8_t> hay,
    std::string_view needle
) {
    const auto* hit = static_cast<const std::uint8_t*>(
        ::memmem(hay.data(), hay.size(), needle.data(), needle.size())
    );
    if (hit == nullptr)
        return {};
    return hay.subspan(static_cast<std::size_t>(hit - hay.data()));
}

svc::DiscRegion licence_region(std::span<const std::uint8_t> sector) {
    const std::span<const std::uint8_t> lic = find_bytes(sector, kLicenceText);
    if (lic.empty())
        return svc::DiscRegion::Unknown;
    const std::uint8_t* tail = lic.data() + kLicenceOffset;
    if (std::memcmp(tail, "Amer  ica ", 10) == 0)
        return svc::DiscRegion::Usa;
    if (std::memcmp(tail, "Inc.", 4) == 0)
        return svc::DiscRegion::Japan;
    if (std::memcmp(tail, "Euro pe", 7) == 0)
        return svc::DiscRegion::Europe;
    return svc::DiscRegion::Unknown;
}

void read_identity_sector(
    const svc::DiscGeometry& disc,
    svc::IDiscReader& reader,
    proto::Lba at,
    std::span<std::uint8_t> out
) {
    using Answer = PsxSectorMap::Answer;
    const Answer answer = PsxSectorMap{disc}.classify(at);
    if (answer != Answer::Engine) {
        std::memset(out.data(), PsxSectorMap::fill_byte(answer), out.size());
        return;
    }
    if (!reader.read_form(svc::DiscForm::RawFrame, at, infra::byte_cast<std::byte>(out))) {
        std::memset(out.data(), PsxSectorMap::fill_byte(Answer::Engine), out.size());
    }
}

}

svc::DiscIdentity PsxDiscIdentity::identify(
    const svc::DiscGeometry& disc,
    svc::IDiscReader& reader
) const noexcept {
    PsxGameId id;
    svc::DiscRegion prefix_region = svc::DiscRegion::Unknown;

    const proto::Lba root_end = kRootFolderLba + kRootDirSectors;
    for (proto::Lba at = kRootFolderLba; at < root_end; at += proto::BlockCount{1}) {
        std::uint8_t sector[svc::kRawFrameBytes];
        read_identity_sector(
            disc,
            reader,
            at,
            std::span<std::uint8_t>{sector, svc::kRawFrameBytes}
        );
        std::span<const std::uint8_t> start{};
        for (const PrefixRegion& row : kPrefixTable) {
            prefix_region = row.region;
            start = find_bytes(
                std::span<const std::uint8_t>{sector, svc::kRawFrameBytes},
                std::string_view{row.prefix, 4}
            );
            if (!start.empty())
                break;
        }
        if (start.empty())
            continue;
        const std::span<const std::uint8_t> end = find_bytes(start, std::string_view{";1", 2});
        if (end.empty())
            continue;
        id.assign(start, start.size() - end.size());
        break;
    }
    if (id.empty())
        prefix_region = svc::DiscRegion::Unknown;

    std::uint8_t sector[svc::kRawFrameBytes];
    read_identity_sector(
        disc,
        reader,
        kLicenseLba,
        std::span<std::uint8_t>{sector, svc::kRawFrameBytes}
    );
    const svc::DiscRegion licence = licence_region(
        std::span<const std::uint8_t>{sector, svc::kRawFrameBytes}
    );
    svc::DiscIdentity out{};
    out.id = id.text;
    out.region = licence != svc::DiscRegion::Unknown ? licence : prefix_region;
    return out;
}

Ex<void> PsxCore::do_adopt(const CoreAdopt&) {

    if (!host().discs)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    discs_ = host().discs;
    geom_ = discs_->geometry();
    return {};
}

MountState PsxCore::mount_disc(std::string_view path) {
    TASTY_SEAT_BODY(PsxCore);
    const auto m = mount(profile().staging.disc_slot, MountedPath{path, proto::FileSize{}});
    if (!m)
        return MountState::Failed;
    if (path.empty())
        return MountState::Done;

    switch (refresh_disc_()) {
        case svc::DiscMountState::Pending:
            return MountState::Pending;
        case svc::DiscMountState::Mounted:
            return finish_mount_() ? MountState::Done : MountState::Pending;
        case svc::DiscMountState::Failed:
        case svc::DiscMountState::Idle:
            break;
    }
    return MountState::Failed;
}

void PsxCore::set_region(proto::RegionCode) {
    TASTY_SEAT_BODY(PsxCore);
}

Ex<void> PsxCore::on_mount(IoIndex slot, const MountedPath& p) {
    if (slot != profile().staging.disc_slot)
        return {};
    if (!discs_) {
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});
    }

    if (mount_seen_ == svc::DiscMountState::Mounted && image_path_.view() == p.path) {
        return {};
    }
    plan_ = StagePlan{};
    image_path_.clear();

    if (p.path.empty()) {
        (void)discs_->arm_mount(svc::DiscOp::Unmount, cue_, std::string_view{});
        (void)refresh_disc_();
        return {};
    }

    if (!infra::iends_with(p.path, ".cue") && !infra::iends_with(p.path, ".chd")) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    const svc::DiscMountState
        st = discs_->arm_mount(svc::DiscOp::Mount, cue_, p.path, profile().disc_identity);
    (void)refresh_disc_();
    if (st == svc::DiscMountState::Failed) {
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }
    (void)image_path_.assign(p.path);
    (void)finish_mount_();
    return {};
}

svc::DiscMountState PsxCore::refresh_disc_() noexcept {
    if (!discs_)
        return svc::DiscMountState::Failed;
    const svc::DiscMountState st = discs_->mount_state();
    if (st != mount_seen_) {
        mount_seen_ = st;
        geom_ = discs_->geometry();
    }
    return st;
}

bool PsxCore::finish_mount_() {
    if (refresh_disc_() != svc::DiscMountState::Mounted)
        return false;

    const svc::Toc& toc = geom_.toc;
    plan_.data_first_track = toc.last != svc::TrackCount{}
        && toc.at(svc::TrackIndex{}).type != svc::TrackType::Cdda;
    plan_.game_id = geom_.identity.id;
    plan_.disc_size_bytes = proto::FileSize{
        static_cast<std::uint64_t>(toc.end.v) * svc::kRawFrameBytes
    };
    return true;
}

Ex<void> PsxCore::stage_disc_payload(const proto::LinkOp::StageDiscPayload& op) {
    TASTY_SEAT_BODY(PsxCore);
    if (!geom_.mounted) {
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }
    const svc::TocFrameMeta meta{geom_.identity.region, op.reset, op.libcrypt_mask};
    const auto frame = svc::build_toc_frame(geom_.toc, meta);
    auto ds = proto::ImageBracket::open(image_sink(), proto::WideIoIndex{251});
    if (!ds)
        return std::unexpected(ds.error());
    if (auto w = ds->write(infra::byte_cast<const std::uint8_t>(std::span(frame))); !w) {
        return w;
    }
    return ds->end();
}

void PsxCore::service_kick() noexcept {
    TASTY_SEAT_BODY(PsxCore);

    if (discs_)
        (void)counts_.take_if_changed(discs_->counters_cell(), counters_);

    if (copy_in_flight_()) {
        ++held_kicks_;
        return;
    }
    hal::Selected cs(host().link, hal::ChipSelect::Io);
    if (auto r = host().link.transfer(hal::SpiWord{cdwire::kUioCdGet}); !r)
        ++wire_faults_;
    ++kicks_;
}

bool PsxCore::copy_in_flight_() const noexcept {
    return host().save_copy && host().save_copy->sample().value.writing;
}

proto::SlotAttributes PsxCore::attributes(proto::SlotIndex) const {
    TASTY_SEAT_BODY(PsxCore);
    return proto::SlotAttributes{};
}

Ex<std::size_t> PsxCore::read_at(
    proto::SlotIndex,
    std::uint64_t offset,
    std::span<std::uint8_t> dst
) {
    TASTY_SEAT_BODY(PsxCore);
    const proto::Lba lba0{static_cast<std::uint32_t>(offset / svc::kRawFrameBytes)};

    const std::size_t win = dst.size() / svc::kRawFrameBytes;
    warm_window_(lba0, win * 2);
    std::size_t served = 0;
    proto::Lba lba = lba0;
    while (served + svc::kRawFrameBytes <= dst.size()) {

        if (!serve_sector_(lba, dst.subspan(served, svc::kRawFrameBytes))) {
            return std::unexpected(Error{Errc::would_block, ERR_SITE(), lba.v});
        }
        served += svc::kRawFrameBytes;
        lba = lba + proto::BlockCount{1};
    }
    return served;
}

Ex<std::size_t> PsxCore::write_at(
    proto::SlotIndex slot,
    std::uint64_t,
    std::span<const std::uint8_t>
) {
    TASTY_SEAT_BODY(PsxCore);
    return std::unexpected(Error{Errc::io, ERR_SITE(), slot.v});
}

proto::BlockGeometry PsxCore::geometry_for(
    proto::SlotIndex slot,
    proto::Lba,
    proto::BlockGeometry wire
) {
    TASTY_SEAT_BODY(PsxCore);
    if (profile().staging.disc_slot.block_slot() != slot)
        return wire;
    proto::BlockGeometry g{proto::BlockSize{svc::kRawFrameBytes}, proto::BlockCount{}};

    const std::uint32_t hunk = geom_.chd_hunk_bytes();
    if (hunk != 0 && hunk <= proto::kBlockStagingBytes) {
        g.window_blocks = proto::BlockSize{svc::kRawFrameBytes}.blocks_in(proto::BlockSize{hunk});
    }
    return g;
}

void PsxCore::warm_window_(proto::Lba lba0, std::size_t sectors) noexcept {
    if (!discs_)
        return;
    proto::Lba lba = lba0;
    for (std::size_t i = 0; i < sectors; ++i) {
        if (PsxSectorMap{geom_}.classify(lba) == PsxSectorMap::Answer::Engine)
            discs_->hint(svc::DiscAsk{.form = svc::DiscForm::RawFrame, .lba = lba});
        lba = lba + proto::BlockCount{1};
    }
}

bool PsxCore::serve_sector_(proto::Lba lba, std::span<std::uint8_t> out) noexcept {
    using Answer = PsxSectorMap::Answer;
    const Answer a = PsxSectorMap{geom_}.classify(lba);
    switch (a) {
        case Answer::Zeros:
            break;
        case Answer::Filler:
            ++filler_sectors_;
            break;
        case Answer::Pregap:
            ++pregap_zero_sectors_;
            break;
        case Answer::Engine:
            break;
    }
    if (a != Answer::Engine) {
        std::memset(out.data(), PsxSectorMap::fill_byte(a), out.size());
        return true;
    }
    std::memset(out.data(), 0xAA, out.size());
    const svc::DiscTake got = discs_->take(
        svc::DiscAsk{.form = svc::DiscForm::RawFrame, .lba = lba},
        infra::byte_cast<std::byte>(out)
    );

    discs_->hint(svc::DiscAsk{.form = svc::DiscForm::RawFrame, .lba = lba + proto::BlockCount{1}});
    if (got.is_failed()) {
        ++filler_sectors_;
        return true;
    }
    return got.is_bytes();
}

}
