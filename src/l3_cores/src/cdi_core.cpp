// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cdi_core.h"

#include <cstring>

#include "cores/mounted_path.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "proto/block_geometry.h"

namespace mister::cores {
namespace {

constexpr std::int32_t kBootLba = 166;
constexpr proto::StatusBit kServoAudioCd{12};
constexpr std::uint32_t kBlock = cdi::kBlockBytes;
constexpr std::size_t kFrame = svc::kCdFrameSize;

}

Ex<void> CdiCore::do_adopt(const CoreAdopt& a) {
    if (!host().discs)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    session_ = infra::OptRef<proto::CoreSession>{a.session};
    discs_ = host().discs;
    geom_ = discs_->geometry();
    return {};
}

MountState CdiCore::mount_disc(std::string_view path) {
    TASTY_SEAT_BODY(CdiCore);
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
            return finish_mount_();
        case svc::DiscMountState::Failed:
        case svc::DiscMountState::Idle:
            break;
    }
    return MountState::Failed;
}

Ex<void> CdiCore::on_mount(IoIndex slot, const MountedPath& p) {
    if (slot.v != profile().staging.disc_slot.v)
        return {};
    if (!discs_)
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});

    if (mount_seen_ == svc::DiscMountState::Mounted && image_path_.view() == p.display)
        return {};
    probed_ = false;
    plan_ = StagePlan{};
    image_path_.clear();
    if (p.display.empty()) {
        toc_count_ = 0;
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
    return {};
}

svc::DiscMountState CdiCore::refresh_disc_() noexcept {
    if (!discs_)
        return svc::DiscMountState::Failed;
    const svc::DiscMountState st = discs_->mount_state();
    if (st != mount_seen_) {
        mount_seen_ = st;
        geom_ = discs_->geometry();
    }
    return st;
}

MountState CdiCore::finish_mount_() {
    if (probed_)
        return MountState::Done;

    if (!geom_.mounted || geom_.toc.last == 0)
        return MountState::Failed;
    std::array<std::uint8_t, kBlock> block{};
    if (!serve_block_(kBootLba, block))
        return MountState::Pending;
    audio_cd_ = std::memcmp(block.data(), cdi::kBootHeader.data(), cdi::kBootHeader.size()) != 0;
    if (session_) {

        session_->post(proto::StatusEdit::bit(kServoAudioCd, audio_cd_));
    }
    disc_type_ = cdi::disc_type(geom_.toc, disc_type_);
    toc_count_ = cdi::build_toc(geom_.toc, disc_type_, toc_);
    plan_.disc_size_bytes = static_cast<std::uint64_t>(geom_.toc.end.v) * cdi::kSectorBytes;
    plan_.data_first_track = geom_.toc.tracks[0].type != svc::TrackType::Cdda;
    probed_ = true;
    return MountState::Done;
}

CdiCore::Answer CdiCore::classify_(std::int32_t lba) const noexcept {
    const svc::Toc& toc = geom_.toc;
    if (lba < 0 || !geom_.mounted || toc.last == 0)
        return Answer::TocArea;
    for (std::uint8_t i = 0; i < toc.last; ++i) {
        const svc::Track& t = toc.tracks[i];
        const std::int64_t lo = static_cast<std::int64_t>(t.start.v)
            - static_cast<std::int32_t>(t.pregap);
        const std::int64_t hi = static_cast<std::int64_t>(t.end.v) - 1;
        if (lba >= lo && lba <= hi)
            return Answer::Track;
    }
    return Answer::LeadOut;
}

void CdiCore::warm_(std::int32_t lba0, std::size_t blocks) noexcept {
    for (std::size_t i = 0; i < blocks; ++i) {
        const auto lba = static_cast<std::int32_t>(static_cast<std::uint32_t>(lba0) + i);
        if (classify_(lba) != Answer::Track)
            continue;
        discs_->hint(svc::DiscForm::FullFrame, proto::Lba{static_cast<std::uint32_t>(lba)});
    }
}

bool CdiCore::serve_block_(
    std::int32_t lba,
    std::span<std::uint8_t, cdi::kBlockBytes> out
) noexcept {
    std::memset(out.data(), 0, out.size());
    const auto sector = out.first<cdi::kSectorBytes>();
    const auto q = out.subspan<cdi::kSectorBytes, cdi::kQBytes>();
    const auto rw = out.subspan<cdi::kSectorBytes + cdi::kQBytes, cdi::kRwBytes>();
    switch (classify_(lba)) {
        case Answer::TocArea:

            if (lba < 0) {
                cdi::compose_toc_q(lba, std::span<const cdi::TocEntry>{toc_.data(), toc_count_}, q);
            } else {
                cdi::compose_q(lba, geom_.toc, q);
            }
            return true;
        case Answer::LeadOut:
            cdi::compose_q(lba, geom_.toc, q);
            return true;
        case Answer::Track:
            break;
    }
    std::array<std::byte, kFrame> frame{};
    const proto::Lba at{static_cast<std::uint32_t>(lba)};
    const std::size_t got = discs_->take(svc::DiscForm::FullFrame, at, frame);
    discs_->hint(svc::DiscForm::FullFrame, proto::Lba{at.v + 1});
    if (got == 0) {
        if (!discs_->take_read_failed(svc::DiscForm::FullFrame, at)) {
            ++declines_;
            return false;
        }
        ++failed_reads_;
    } else {

        std::memcpy(sector.data(), frame.data(), cdi::kSectorBytes);
        cdi::widen_rw(
            infra::byte_cast<const std::uint8_t>(std::span(frame))
                .subspan<cdi::kSectorBytes, cdi::kRawSubcodeBytes>(),
            rw
        );
    }
    cdi::compose_q(lba, geom_.toc, q);
    return true;
}

proto::SlotAttributes CdiCore::attributes(proto::SlotIndex) const {
    TASTY_SEAT_BODY(CdiCore);
    return proto::SlotAttributes{};
}

Ex<std::size_t> CdiCore::read_at(
    proto::SlotIndex,
    std::uint64_t offset,
    std::span<std::uint8_t> dst
) {
    TASTY_SEAT_BODY(CdiCore);

    const auto lba0 = static_cast<std::int32_t>(static_cast<std::uint32_t>(offset / kBlock));
    const std::size_t blocks = dst.size() / kBlock;
    warm_(lba0, blocks * 2);
    for (std::size_t i = 0; i < blocks; ++i) {
        const auto lba = static_cast<std::int32_t>(static_cast<std::uint32_t>(lba0) + i);
        if (!serve_block_(lba, dst.subspan(i * kBlock).first<kBlock>())) {
            return std::unexpected(
                Error{Errc::would_block, ERR_SITE(), static_cast<std::uint32_t>(lba)}
            );
        }
    }
    return blocks * kBlock;
}

Ex<std::size_t> CdiCore::write_at(
    proto::SlotIndex slot,
    std::uint64_t,
    std::span<const std::uint8_t>
) {
    TASTY_SEAT_BODY(CdiCore);
    return std::unexpected(Error{Errc::io, ERR_SITE(), slot.v});
}

proto::BlockGeometry CdiCore::geometry_for(
    proto::SlotIndex slot,
    proto::Lba,
    proto::BlockGeometry wire
) {
    TASTY_SEAT_BODY(CdiCore);
    if (slot.v != profile().staging.disc_slot.v)
        return wire;

    return proto::BlockGeometry{kBlock, cdi::hunk_window(geom_.chd_hunk_bytes())};
}

}
