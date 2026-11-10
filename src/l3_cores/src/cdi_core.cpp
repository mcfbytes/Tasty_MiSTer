// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cdi_core.h"

#include <cstring>

#include "cores/core_adopt.h"
#include "cores/mounted_path.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "proto/block_geometry.h"

namespace mister::cores {
namespace {

constexpr HeadLba kBootLba{166};
constexpr proto::StatusBit kServoAudioCd{12};
constexpr std::uint32_t kBlock = cdi::kBlockBytes;

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
    const auto m = mount(profile().staging.disc_slot, MountedPath{path, proto::FileSize{}});
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
    if (slot != profile().staging.disc_slot)
        return {};
    if (!discs_)
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});

    if (mount_seen_ == svc::DiscMountState::Mounted && image_path_.view() == p.path)
        return {};
    probed_ = false;
    plan_ = StagePlan{};
    image_path_.clear();
    if (p.path.empty()) {
        toc_ = {};
        (void)discs_->arm_mount(
            svc::DiscOp::Unmount,
            infra::OptRef<const svc::CuePolicy>{cue_},
            std::string_view{}
        );
        (void)refresh_disc_();
        return {};
    }

    if (!infra::iends_with(p.path, ".cue") && !infra::iends_with(p.path, ".chd")) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    const svc::DiscMountState st = discs_->arm_mount(
        svc::DiscOp::Mount,
        infra::OptRef<const svc::CuePolicy>{cue_},
        p.path
    );
    (void)refresh_disc_();
    if (st == svc::DiscMountState::Failed) {
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }
    (void)image_path_.assign(p.path);
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

    if (!geom_.mounted || geom_.toc.last == svc::TrackCount{})
        return MountState::Failed;
    std::array<std::uint8_t, kBlock> block{};
    if (!serve_block_(kBootLba, block))
        return MountState::Pending;
    audio_cd_ = std::memcmp(block.data(), cdi::kBootHeader.data(), cdi::kBootHeader.size()) != 0;
    if (session_) {

        session_->post(proto::StatusEdit::bit(kServoAudioCd, audio_cd_));
    }
    disc_type_ = cdi::disc_type(geom_.toc, disc_type_);
    toc_ = cdi::build_toc(geom_.toc, disc_type_, toc_rows_);
    plan_.disc_size_bytes = proto::FileSize{
        static_cast<std::uint64_t>(geom_.toc.end.v) * cdi::kSectorBytes
    };
    plan_.data_first_track = geom_.toc.at(svc::TrackIndex{}).type != svc::TrackType::Cdda;
    probed_ = true;
    return MountState::Done;
}

CdiCore::Answer CdiCore::classify_(HeadLba lba) const noexcept {
    const svc::Toc& toc = geom_.toc;
    if (!lba.readable() || !geom_.mounted || toc.last == svc::TrackCount{})
        return Answer::TocArea;
    for (svc::TrackIndex i{}; toc.last.contains(i); i = i + svc::TrackCount{1}) {
        const svc::Track& t = toc.at(i);
        const HeadLba lo = HeadLba::of(t.start) - SectorCount::of(t.pregap);
        const HeadLba hi = HeadLba::of(t.end) - SectorCount{1};
        if (lba >= lo && lba <= hi)
            return Answer::Track;
    }
    return Answer::LeadOut;
}

void CdiCore::warm_(HeadLba first, SectorCount blocks) noexcept {
    for (SectorCount i{}; i < blocks; i = i + SectorCount{1}) {
        const HeadLba lba = first + i;

        const std::optional<proto::Lba> at = lba.readable();
        if (!at || classify_(lba) != Answer::Track)
            continue;
        discs_->hint(svc::DiscAsk{.form = svc::DiscForm::FullFrame, .lba = *at});
    }
}

bool CdiCore::serve_block_(HeadLba lba, std::span<std::uint8_t, cdi::kBlockBytes> out) noexcept {
    std::memset(out.data(), 0, out.size());
    const auto sector = out.first<cdi::kSectorBytes>();
    const auto q = out.subspan<cdi::kSectorBytes, cdi::kQBytes>();
    const auto rw = out.subspan<cdi::kSectorBytes + cdi::kQBytes, cdi::kRwBytes>();
    const std::optional<proto::Lba> at = lba.readable();
    if (!at) {

        const cdi::QFrame frame = cdi::compose_toc_q(lba, toc_);
        std::memcpy(q.data(), frame.bytes.data(), cdi::kQBytes);
        return true;
    }
    switch (classify_(lba)) {
        case Answer::TocArea:
        case Answer::LeadOut: {

            const cdi::QFrame frame = cdi::compose_q(lba, geom_.toc);
            std::memcpy(q.data(), frame.bytes.data(), cdi::kQBytes);
            return true;
        }
        case Answer::Track:
            break;
    }
    std::array<std::byte, svc::kFullFrameBytes> frame{};
    const svc::DiscTake got = discs_->take(
        svc::DiscAsk{.form = svc::DiscForm::FullFrame, .lba = *at},
        frame
    );
    discs_->hint(svc::DiscAsk{.form = svc::DiscForm::FullFrame, .lba = *at + proto::BlockCount{1}});
    if (got.is_bytes()) {

        std::memcpy(sector.data(), frame.data(), cdi::kSectorBytes);
        cdi::widen_rw(
            infra::byte_cast<const std::uint8_t>(std::span(frame))
                .subspan<cdi::kSectorBytes, cdi::kRawSubcodeBytes>(),
            rw
        );
    } else if (got.is_failed()) {
        ++failed_reads_;
    } else {
        ++declines_;
        return false;
    }
    const cdi::QFrame q_frame = cdi::compose_q(lba, geom_.toc);
    std::memcpy(q.data(), q_frame.bytes.data(), cdi::kQBytes);
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

    const proto::Lba start{static_cast<std::uint32_t>(offset / kBlock)};
    const std::size_t blocks = dst.size() / kBlock;
    const proto::BlockCount window{static_cast<std::uint32_t>(blocks)};
    warm_(HeadLba::of(start), SectorCount::of(window * 2u));
    std::span<std::uint8_t> rest = dst;
    for (proto::BlockCount i{}; i < window; i += proto::BlockCount{1}) {
        const proto::Lba at = start + i;
        if (!serve_block_(HeadLba::of(at), rest.first<kBlock>())) {
            return std::unexpected(
                Error{Errc::would_block, ERR_SITE(), static_cast<std::uint32_t>(at.v)}
            );
        }
        rest = rest.subspan(kBlock);
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

    if (slot != profile().staging.disc_slot.block_slot())
        return wire;

    return proto::BlockGeometry{
        proto::BlockSize{kBlock},
        proto::BlockCount{cdi::hunk_window(geom_.chd_hunk_bytes())}
    };
}

}
