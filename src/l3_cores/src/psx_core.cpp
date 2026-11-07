// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_core.h"
#include "cores/core_adopt.h"
#include "hal/selected.h"

#include <algorithm>
#include <cstring>

#include "cores/cdwire.h"
#include "cores/mounted_path.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "proto/image_bracket.h"
#include "proto/image_sink.h"

namespace mister::cores {
namespace {

constexpr std::uint32_t kSectorBytes = static_cast<std::uint32_t>(svc::kCdDataSize);
constexpr std::int64_t kRootFolderLba = 150 + 22;
constexpr std::int64_t kLicenseLba = 154;

constexpr std::int64_t kRootDirSectors = 25;

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
    const auto m = mount(
        profile().staging.disc_slot,
        MountedPath{proto::PathId{}, path, proto::FileSize{}}
    );
    if (!m)
        return MountState::Failed;
    if (path.empty())
        return MountState::Done;

    switch (refresh_disc_()) {
        case svc::DiscMountState::Pending:
            return MountState::Pending;
        case svc::DiscMountState::Mounted:
            return scan_done_ ? MountState::Done : MountState::Pending;
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
    if (slot.v != profile().staging.disc_slot.v)
        return {};
    if (!discs_) {
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});
    }

    if (mount_seen_ == svc::DiscMountState::Mounted && image_path_.view() == p.display) {
        return {};
    }
    plan_ = StagePlan{};
    game_id_.clear();
    region_ = svc::DiscRegion::Unknown;
    image_path_.clear();

    scan_lba_ = kRootFolderLba;
    scan_done_ = false;
    scan_prefix_region_ = svc::DiscRegion::Unknown;
    if (p.display.empty()) {
        (void)discs_->arm_mount(svc::DiscOp::Unmount, cue_, std::string_view{});
        (void)refresh_disc_();
        return {};
    }

    if (!infra::iends_with(p.display, ".cue") && !infra::iends_with(p.display, ".chd")) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    const svc::DiscMountState st = discs_->arm_mount(svc::DiscOp::Mount, cue_, p.display);
    (void)refresh_disc_();
    if (st == svc::DiscMountState::Failed) {
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }
    (void)image_path_.assign(p.display);
    return finish_mount_();
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

Ex<void> PsxCore::finish_mount_() {
    if (refresh_disc_() != svc::DiscMountState::Mounted)
        return {};
    if (!scan_disc_identity_())
        return {};

    const svc::Toc& toc = geom_.toc;
    plan_.data_first_track = toc.last != svc::TrackCount{}
        && toc.at(svc::TrackIndex{}).type != svc::TrackType::Cdda;
    plan_.game_id = game_id_.text;
    plan_.disc_size_bytes = proto::FileSize{static_cast<std::uint64_t>(toc.end.v) * kSectorBytes};
    return {};
}

bool PsxCore::scan_root_() {
    std::uint8_t sector[kSectorBytes];
    svc::DiscRegion prefix_region = scan_prefix_region_;
    for (std::int64_t lba = scan_lba_; lba < kRootFolderLba + kRootDirSectors; ++lba) {

        if (!serve_sector_(lba, std::span<std::uint8_t>{sector, kSectorBytes})) {
            scan_lba_ = lba;
            scan_prefix_region_ = prefix_region;
            return false;
        }
        std::span<const std::uint8_t> start{};
        for (const PrefixRegion& row : kPrefixTable) {
            prefix_region = row.region;
            start = find_bytes(
                std::span<const std::uint8_t>{sector, kSectorBytes},
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
        game_id_.assign(start, start.size() - end.size());
        break;
    }
    if (game_id_.empty())
        prefix_region = svc::DiscRegion::Unknown;
    scan_prefix_region_ = prefix_region;
    scan_lba_ = kRootFolderLba + kRootDirSectors;
    return true;
}

svc::DiscRegion PsxCore::licence_region_(std::span<const std::uint8_t> sector) const {
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

bool PsxCore::scan_disc_identity_() {
    if (scan_done_)
        return true;
    if (!scan_root_())
        return false;
    std::uint8_t sector[kSectorBytes];
    if (!serve_sector_(kLicenseLba, std::span<std::uint8_t>{sector, kSectorBytes})) {
        return false;
    }
    const svc::DiscRegion licence = licence_region_(
        std::span<const std::uint8_t>{sector, kSectorBytes}
    );
    region_ = licence != svc::DiscRegion::Unknown ? licence : scan_prefix_region_;
    scan_done_ = true;
    return true;
}

Ex<void> PsxCore::stage_disc_payload(const proto::LinkOp::StageDiscPayload& op) {
    TASTY_SEAT_BODY(PsxCore);
    if (!geom_.mounted) {
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }
    const svc::TocFrameMeta meta{region_, op.reset, op.libcrypt_mask};
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

    if (!image_path_.empty() && !scan_done_)
        (void)finish_mount_();

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
    const std::int64_t lba0 = static_cast<std::int64_t>(offset / kSectorBytes);

    const std::size_t win = dst.size() / kSectorBytes;
    warm_window_(lba0, win * 2);
    std::size_t served = 0;
    std::int64_t lba = lba0;
    while (served + kSectorBytes <= dst.size()) {

        if (!serve_sector_(lba, dst.subspan(served, kSectorBytes))) {
            return std::unexpected(
                Error{Errc::would_block, ERR_SITE(), static_cast<std::uint32_t>(lba)}
            );
        }
        served += kSectorBytes;
        ++lba;
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
    if (slot.v != profile().staging.disc_slot.v)
        return wire;
    proto::BlockGeometry g{proto::BlockSize{kSectorBytes}, proto::BlockCount{}};

    const std::uint32_t hunk = geom_.chd_hunk_bytes();
    if (hunk != 0 && hunk <= proto::kBlockStagingBytes) {
        g.window_blocks = proto::BlockCount{hunk / kSectorBytes};
    }
    return g;
}

void PsxCore::warm_window_(std::int64_t lba0, std::size_t sectors) noexcept {
    if (!discs_)
        return;
    for (std::size_t i = 0; i < sectors; ++i) {
        const std::int64_t lba = lba0 + static_cast<std::int64_t>(i);
        if (PsxSectorMap{geom_}.classify(lba) != PsxSectorMap::Answer::Engine)
            continue;
        discs_->hint(svc::DiscForm::RawFrame, proto::Lba{static_cast<std::uint32_t>(lba)});
    }
}

bool PsxCore::serve_sector_(std::int64_t lba, std::span<std::uint8_t> out) noexcept {
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
    const proto::Lba at{static_cast<std::uint32_t>(lba)};
    const std::size_t got = discs_->take(
        svc::DiscForm::RawFrame,
        at,
        infra::byte_cast<std::byte>(out)
    );

    discs_->hint(svc::DiscForm::RawFrame, proto::Lba{at.v + 1});
    if (got == 0) {
        std::memset(out.data(), 0xAA, out.size());
        if (discs_->take_read_failed(svc::DiscForm::RawFrame, at)) {
            ++filler_sectors_;
            return true;
        }
        return false;
    }
    return true;
}

}
