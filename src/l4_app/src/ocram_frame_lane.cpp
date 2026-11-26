// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ocram_frame_lane.h"

#include <cstdint>
#include <span>

#include "infra/prelude.h"
#include "proto/link/link_sel_carriers.h"

namespace mister::app {

namespace ocram = hal::ocram;

namespace {

constexpr std::size_t kPackedWords = OcramFrameLane::kStageWords / ocram::Words::kWordHalves;

constexpr ocram::Words kWidestFrame = ocram::Words::of(
    OcramFrameLane::kMaxPlays * (1u + kPackedWords)
);

}

bool OcramFrameLane::bind(const OcramFrameGrant& grant) noexcept {
    TASTY_SEAT_BODY(OcramFrameLane);
    unbind();
    if (!grant.granted()
        || grant.lend.desc().kind() != ocram::RingDesc::Kind::Lane
        || grant.lend.desc().words() < kWidestFrame) {
        return false;
    }
    lend_ = grant.lend;
    shape_ = grant.shape;
    return true;
}

void OcramFrameLane::unbind() noexcept {
    TASTY_SEAT_BODY(OcramFrameLane);
    drop_posted_();
    open_ = false;
    unplayed_ = false;
    suspended_ = false;
    ring_.reset();
    lend_ = ocram::RingLend{};
    shape_.reset();
}

Ex<void> OcramFrameLane::begin(proto::LinkSel sel, hal::LinkWord op) {
    TASTY_SEAT_BODY(OcramFrameLane);
    open_ = false;

    if (posted_) {
        counts_.refused.add();
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    if (!lend_.lent() || suspended_ || plays_(sel).count == 0) {
        counts_.refused.add();
        return std::unexpected(op.refusal(Errc::negotiation, ERR_SITE()));
    }

    if (!ring_)
        redeem_();
    stage_[0] = op;
    staged_ = 1;
    sel_ = sel;
    open_ = true;
    return {};
}

Ex<void> OcramFrameLane::word(hal::LinkWord w) {
    TASTY_SEAT_BODY(OcramFrameLane);
    if (!open_)
        return std::unexpected(w.refusal(Errc::negotiation, ERR_SITE()));
    if (staged_ == stage_.size()) {
        open_ = false;
        counts_.refused.add();
        return std::unexpected(w.refusal(Errc::bad_format, ERR_SITE()));
    }
    stage_[staged_] = w;
    ++staged_;
    return {};
}

Ex<void> OcramFrameLane::end() {
    TASTY_SEAT_BODY(OcramFrameLane);
    if (!open_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    open_ = false;
    if (!publish_()) {
        posted_ = true;
        counts_.posted.add();
    }
    return {};
}

void OcramFrameLane::settle() noexcept {
    TASTY_SEAT_BODY(OcramFrameLane);
    if (!ring_)
        return;
    if (posted_) {
        if (!publish_())
            return;
        posted_ = false;
        return;
    }
    unplayed_ = !ring_->drained();
}

void OcramFrameLane::abandon() noexcept {
    TASTY_SEAT_BODY(OcramFrameLane);
    drop_posted_();
    open_ = false;
    unplayed_ = false;
}

void OcramFrameLane::suspend() noexcept {
    TASTY_SEAT_BODY(OcramFrameLane);
    abandon();
    ring_.reset();
    suspended_ = true;
}

void OcramFrameLane::resume() noexcept {
    TASTY_SEAT_BODY(OcramFrameLane);
    unplayed_ = false;
    suspended_ = false;
}

std::optional<hal::LinkWord> OcramFrameLane::opcode_reply() const noexcept {
    TASTY_SEAT_BODY(OcramFrameLane);
    return std::nullopt;
}

bool OcramFrameLane::busy() const noexcept {
    TASTY_SEAT_BODY(OcramFrameLane);
    return posted_ || unplayed_;
}

OcramFrameLane::Plays OcramFrameLane::plays_(proto::LinkSel sel) const noexcept {
    Plays p{};
    if (!shape_)
        return p;
    if (sel == proto::LinkSel::Osd) {
        p.add_served(*shape_, ocram::LaneArg::Sel::OsdHdmi);
        p.add_served(*shape_, ocram::LaneArg::Sel::OsdVga);
        return p;
    }
    if (const auto one = proto::link::lane_sel(sel))
        p.add_served(*shape_, *one);
    return p;
}

bool OcramFrameLane::publish_() noexcept {
    if (!ring_)
        return false;
    const std::span<const hal::LinkWord> link = std::span<const hal::LinkWord>(stage_).first(
        staged_
    );
    const Plays plays = plays_(sel_);
    std::array<std::uint32_t, kPackedWords> packed{};
    const ocram::Words len = ocram::Words::carrying_halves(link.size());
    const std::span<std::uint32_t> payload = len.prefix_of(std::span<std::uint32_t>(packed));
    ocram::RecordHeader::pack_lframe(link, payload);
    ocram::Words need{};
    for (std::size_t i = 0; i < plays.count; ++i)
        need = need + ocram::RecordHeader::lframe_for(plays.sel[i], link).extent();
    if (ring_->room() < need)
        return false;
    for (std::size_t i = 0; i < plays.count; ++i) {
        if (!ring_->publish(ocram::RecordHeader::lframe_for(plays.sel[i], link), payload)) {
            return false;
        }
        counts_.records.add();
    }
    unplayed_ = true;
    return true;
}

void OcramFrameLane::redeem_() noexcept {
    ring_.emplace(ocram::RingWriter<seat_owner::RT>::redeem(lend_));
}

void OcramFrameLane::drop_posted_() noexcept {
    if (!posted_)
        return;
    posted_ = false;
    counts_.abandoned.add();
}

}
