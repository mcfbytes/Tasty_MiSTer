// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_core.h"
#include "hal/selected.h"

#include <strings.h>

#include <algorithm>
#include <cstring>

#include "cores/cdwire.h"
#include "cores/mounted_path.h"
#include "proto/image_bracket.h"
#include "proto/image_sink.h"

namespace mister::cores {
namespace {

constexpr std::uint32_t kSectorBytes = static_cast<std::uint32_t>(svc::kCdDataSize);
constexpr std::int64_t kRootFolderLba = 150 + 22;
constexpr std::int64_t kLicenseLba = 154;

struct PrefixRegion {
    const char* prefix;
    svc::DiscRegion region;
};
constexpr PrefixRegion kPrefixTable[] = {
    {"SCES", svc::DiscRegion::Europe}, {"SLES", svc::DiscRegion::Europe},
    {"SCUS", svc::DiscRegion::Usa},    {"SLUS", svc::DiscRegion::Usa},
    {"SCPM", svc::DiscRegion::Japan},  {"SLPM", svc::DiscRegion::Japan},
    {"SCPS", svc::DiscRegion::Japan},  {"SLPS", svc::DiscRegion::Japan},
    {"SIPS", svc::DiscRegion::Japan},  {"PUPX", svc::DiscRegion::Usa},
    {"PEPX", svc::DiscRegion::Europe}, {"PAPX", svc::DiscRegion::Japan},
    {"PCPX", svc::DiscRegion::Japan},  {"SCZS", svc::DiscRegion::Japan},
    {"SCED", svc::DiscRegion::Europe}, {"SLED", svc::DiscRegion::Europe},
};

std::span<const std::uint8_t> find_bytes(std::span<const std::uint8_t> hay,
                                         std::string_view needle) {
    const auto* hit = static_cast<const std::uint8_t*>(
        ::memmem(hay.data(), hay.size(), needle.data(), needle.size()));
    if (hit == nullptr) return {};
    return hay.subspan(static_cast<std::size_t>(hit - hay.data()));
}

bool ends_with_ci(std::string_view s, std::string_view tail) {
    if (s.size() < tail.size()) return false;
    return ::strncasecmp(s.data() + s.size() - tail.size(), tail.data(), tail.size()) == 0;
}

}  // namespace

Ex<void> PsxCore::do_init(proto::CoreSession&) {

    discs_ = host().discs;
    if (discs_ == nullptr) {
        own_discs_.emplace();
        discs_ = &*own_discs_;
    }
    geom_ = discs_->geometry();
    return {};
}

MountState PsxCore::mount_disc(std::string_view path) {
    TASTY_SEAT_BODY(PsxCore);
    const auto m =
        mount(profile().staging.disc_slot, MountedPath{proto::PathId{}, path, proto::FileSize{}});
    if (!m) return MountState::Failed;
    if (path.empty()) return MountState::Done;

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

void PsxCore::set_region(std::uint8_t) { TASTY_SEAT_BODY(PsxCore); }

Ex<void> PsxCore::on_mount(IoIndex slot, const MountedPath& p) {
    if (slot.v != profile().staging.disc_slot.v) return {};
    if (discs_ == nullptr) {
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});
    }

    if (mount_seen_ == svc::DiscMountState::Mounted && image_path_ == p.display) {
        return {};
    }
    plan_ = StagePlan{};
    game_id_ = {};
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

    if (!ends_with_ci(p.display, ".cue") && !ends_with_ci(p.display, ".chd")) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    const svc::DiscMountState st = discs_->arm_mount(svc::DiscOp::Mount, cue_, p.display);
    (void)refresh_disc_();
    if (st == svc::DiscMountState::Failed) {
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }
    image_path_.assign(p.display);
    return finish_mount_();
}

svc::DiscMountState PsxCore::refresh_disc_() noexcept {
    if (discs_ == nullptr) return svc::DiscMountState::Failed;
    const svc::DiscMountState st = discs_->mount_state();
    if (st != mount_seen_) {
        mount_seen_ = st;
        geom_ = discs_->geometry();
    }
    return st;
}

Ex<void> PsxCore::finish_mount_() {
    if (refresh_disc_() != svc::DiscMountState::Mounted) return {};
    if (!scan_disc_identity_()) return {};

    const svc::Toc& toc = geom_.toc;
    plan_.data_first_track = toc.last != 0 && toc.tracks[0].type != svc::TrackType::Cdda;
    plan_.game_id = game_id_;
    plan_.disc_size_bytes = static_cast<std::uint64_t>(toc.end.v) * kSectorBytes;
    return {};
}

bool PsxCore::scan_disc_identity_() {
    if (scan_done_) return true;
    std::uint8_t sector[kSectorBytes];
    svc::DiscRegion prefix_region = scan_prefix_region_;

    for (std::int64_t lba = scan_lba_; lba < kRootFolderLba + 25; ++lba) {

        if (!serve_sector_(lba, std::span<std::uint8_t>{sector, kSectorBytes})) {
            scan_lba_ = lba;
            scan_prefix_region_ = prefix_region;
            return false;
        }
        std::span<const std::uint8_t> start{};
        for (const PrefixRegion& row : kPrefixTable) {
            prefix_region = row.region;
            start = find_bytes(std::span<const std::uint8_t>{sector, kSectorBytes},
                               std::string_view{row.prefix, 4});
            if (!start.empty()) break;
        }
        if (start.empty()) continue;
        const std::span<const std::uint8_t> end = find_bytes(start, std::string_view{";1", 2});
        if (end.empty()) continue;

        char id[12] = {};
        const std::size_t id_len = start.size() - end.size();
        std::size_t size = id_len;
        if (size > 11) size = 11;
        std::memcpy(id, start.data(), size);
        if (id_len == 11) {
            if (id[4] == '_') id[4] = '-';
            if (id[8] == '.') {
                id[8] = id[9];
                id[9] = id[10];
                id[10] = '\0';
                --size;
            }
        }
        if (size > 10) size = 10;
        id[size] = '\0';
        (void)game_id_.assign(std::string_view{id, size});
        break;
    }
    if (game_id_.view().empty()) prefix_region = svc::DiscRegion::Unknown;

    if (!serve_sector_(kLicenseLba, std::span<std::uint8_t>{sector, kSectorBytes})) {
        scan_lba_ = kRootFolderLba + 25;
        scan_prefix_region_ = prefix_region;
        return false;
    }
    const std::span<const std::uint8_t> lic = find_bytes(
        std::span<const std::uint8_t>{sector, kSectorBytes},
        std::string_view{"          Licensed  by          Sony Computer Entertainment ", 60});
    svc::DiscRegion region = svc::DiscRegion::Unknown;
    if (!lic.empty()) {
        const std::uint8_t* tail = lic.data() + 60;
        if (std::memcmp(tail, "Amer  ica ", 10) == 0)
            region = svc::DiscRegion::Usa;
        else if (std::memcmp(tail, "Inc.", 4) == 0)
            region = svc::DiscRegion::Japan;
        else if (std::memcmp(tail, "Euro pe", 7) == 0)
            region = svc::DiscRegion::Europe;
    }
    region_ = region != svc::DiscRegion::Unknown ? region : prefix_region;
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
    if (!ds) return std::unexpected(ds.error());
    if (auto w = ds->write(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(frame.data()), frame.size()});
        !w) {
        return w;
    }
    return ds->end();
}

void PsxCore::service_kick() noexcept {
    TASTY_SEAT_BODY(PsxCore);

    if (discs_ != nullptr) (void)counts_.take_if_changed(discs_->counters_cell(), counters_);

    if (!image_path_.empty() && !scan_done_) (void)finish_mount_();
    hal::Selected cs(host().link, hal::ChipSelect::Io);
    if (auto r = host().link.transfer(hal::SpiWord{cdwire::kUioCdGet}); !r) ++wire_faults_;
    ++kicks_;
}

proto::SlotAttributes PsxCore::attributes(proto::SlotIndex) const {
    TASTY_SEAT_BODY(PsxCore);
    return proto::SlotAttributes{};
}

Ex<std::size_t> PsxCore::read_at(proto::SlotIndex, std::uint64_t offset,
                                 std::span<std::uint8_t> dst) {
    TASTY_SEAT_BODY(PsxCore);
    const std::int64_t lba0 = static_cast<std::int64_t>(offset / kSectorBytes);

    const std::size_t win = dst.size() / kSectorBytes;
    warm_window_(lba0, win * 2);
    std::size_t served = 0;
    std::int64_t lba = lba0;
    while (served + kSectorBytes <= dst.size()) {

        if (!serve_sector_(lba, dst.subspan(served, kSectorBytes))) {
            return std::unexpected(
                Error{Errc::would_block, ERR_SITE(), static_cast<std::uint32_t>(lba)});
        }
        served += kSectorBytes;
        ++lba;
    }
    return served;
}

Ex<std::size_t> PsxCore::write_at(proto::SlotIndex slot, std::uint64_t,
                                  std::span<const std::uint8_t>) {
    TASTY_SEAT_BODY(PsxCore);
    return std::unexpected(Error{Errc::io, ERR_SITE(), slot.v});
}

proto::BlockGeometry PsxCore::geometry_for(proto::SlotIndex slot, proto::Lba,
                                           proto::BlockGeometry wire) {
    TASTY_SEAT_BODY(PsxCore);
    if (slot.v != profile().staging.disc_slot.v) return wire;
    proto::BlockGeometry g{kSectorBytes, 0};

    const std::uint32_t hunk = geom_.chd_hunk_bytes();
    if (hunk != 0 && hunk <= proto::kBlockStagingBytes) {
        g.window_blocks = hunk / kSectorBytes;
    }
    return g;
}

PsxCore::Answer PsxCore::classify_(std::int64_t lba) const noexcept {
    const svc::Toc* toc = geom_.mounted ? &geom_.toc : nullptr;
    if (toc == nullptr || toc->last == 0 ||
        lba < static_cast<std::int64_t>(toc->tracks[0].start.v)) {
        return Answer::Zeros;
    }
    const auto ti = geom_.track_for_lba(proto::Lba{static_cast<std::uint32_t>(lba)});
    if (!ti.has_value()) return Answer::Filler;
    if (ti->v + 1 < toc->last) {
        const svc::Track& next = toc->tracks[ti->v + 1];

        if (next.pregap_declared && lba > static_cast<std::int64_t>(next.start.v) -
                                              static_cast<std::int64_t>(next.pregap)) {
            return Answer::Pregap;
        }
    }
    return Answer::Engine;
}

void PsxCore::warm_window_(std::int64_t lba0, std::size_t sectors) noexcept {
    if (discs_ == nullptr) return;
    for (std::size_t i = 0; i < sectors; ++i) {
        const std::int64_t lba = lba0 + static_cast<std::int64_t>(i);
        if (classify_(lba) != Answer::Engine) continue;
        discs_->hint(svc::DiscForm::RawFrame, proto::Lba{static_cast<std::uint32_t>(lba)});
    }
}

bool PsxCore::serve_sector_(std::int64_t lba, std::span<std::uint8_t> out) noexcept {
    switch (classify_(lba)) {
        case Answer::Zeros:
            std::memset(out.data(), 0, out.size());
            return true;
        case Answer::Filler:
            std::memset(out.data(), 0xAA, out.size());
            ++filler_sectors_;
            return true;
        case Answer::Pregap:
            std::memset(out.data(), 0, out.size());
            ++pregap_zero_sectors_;
            return true;
        case Answer::Engine:
            break;
    }
    std::memset(out.data(), 0xAA, out.size());
    const proto::Lba at{static_cast<std::uint32_t>(lba)};
    const std::size_t got =
        discs_->take(svc::DiscForm::RawFrame, at,
                     std::span<std::byte>{reinterpret_cast<std::byte*>(out.data()), out.size()});

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

}  // namespace mister::cores
