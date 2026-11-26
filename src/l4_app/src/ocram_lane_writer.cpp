// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ocram_lane_writer.h"

#include <algorithm>
#include <utility>

#include "infra/prelude.h"
#include "proto/link/fio_bracket_words.h"

namespace mister::app {

namespace ocram = hal::ocram;

namespace {

constexpr infra::ByteCount kDataAlign{4};
constexpr infra::ByteCount kMaxDataBytes{4096};

constexpr hal::ocram::Words kOpenRingWords{8};

}

infra::ByteCount OcramLaneWriter::cap_bytes_(ocram::Words ring, hal::Width w) noexcept {
    const ocram::Words quarter = ring.quarter();
    if (quarter <= ocram::Words{1})
        return infra::ByteCount{};

    const infra::ByteCount carried = infra::ByteCount{hal::word_bytes(w)}
        * ((quarter - ocram::Words{1}).halves() - 1u);
    return std::min(kDataAlign * (carried / kDataAlign), kMaxDataBytes);
}

bool OcramLaneWriter::servable(ocram::RingDesc ring) noexcept {
    return cap_bytes_(ring.words(), hal::Width::Byte) >= kDataAlign
        && cap_bytes_(ring.words(), hal::Width::Word) >= kDataAlign;
}

bool OcramLaneWriter::bind(ocram::RingLend lend, std::optional<os::UioLineRef> line) noexcept {
    TASTY_SEAT_BODY(OcramLaneWriter);
    unbind();
    if (!lend.lent()
        || lend.desc().kind() != ocram::RingDesc::Kind::Lane
        || !servable(lend.desc())) {
        return false;
    }
    lend_ = lend;

    line_open_ = line.has_value() && line_.open(*line, wiring_.doorbells);
    return true;
}

void OcramLaneWriter::unbind() noexcept {
    TASTY_SEAT_BODY(OcramLaneWriter);
    if (ring_)
        ring_->disarm_mark();
    line_.close();
    line_open_ = false;
    ring_.reset();
    lend_ = ocram::RingLend{};
    waiting_ = Wait::None;
    pushed_at_.reset();
}

bool OcramLaneWriter::granted() const noexcept {
    return lend_.lent();
}

bool OcramLaneWriter::lost() const noexcept {
    TASTY_SEAT_BODY(OcramLaneWriter);
    if (!granted())
        return true;
    return ring_ && (!ring_->echoed() || ring_->faulted());
}

bool OcramLaneWriter::open(const proto::link::FioOpen& open) noexcept {
    TASTY_SEAT_BODY(OcramLaneWriter);
    if (!granted())
        return false;
    if (!ring_) {
        ring_.emplace(ocram::RingWriter<seat_owner::Main>::redeem(lend_));
        ring_->disarm_mark();
    }
    if (ring_->room() < kOpenRingWords)
        return false;
    Frame frame{*this};
    return proto::link::FioBracketWords::emit_open(frame, open).has_value();
}

proto::FileSize OcramLaneWriter::data(std::span<const std::uint8_t> bytes, hal::Width w) noexcept {
    TASTY_SEAT_BODY(OcramLaneWriter);
    if (!ring_)
        return proto::FileSize{};
    const infra::ByteCount cap = cap_bytes_(ring_->size(), w);
    proto::FileSize taken{};
    std::span<const std::uint8_t> rest = bytes;
    while (!cap.empty() && !rest.empty()) {
        const std::span<const std::uint8_t> burst = cap.first_in(rest);
        Frame frame{*this};
        if (!proto::link::FioBracketWords::emit_data(frame, w, burst))
            break;
        taken = taken + proto::FileSize::of(burst);
        rest = cap.past_in(rest);
    }
    return taken;
}

bool OcramLaneWriter::end() noexcept {
    TASTY_SEAT_BODY(OcramLaneWriter);
    if (!ring_)
        return false;
    Frame frame{*this};
    return proto::link::FioBracketWords::emit_end(frame).has_value();
}

bool OcramLaneWriter::played() noexcept {
    TASTY_SEAT_BODY(OcramLaneWriter);
    if (!ring_)
        return true;
    if (!ring_->drained())
        return false;

    if (pushed_at_ && wiring_.inbox && wiring_.inbox->pushes() != *pushed_at_) {
        counts_.busy.add();
    }
    pushed_at_.reset();
    return true;
}

bool OcramLaneWriter::unplayed() const noexcept {
    TASTY_SEAT_BODY(OcramLaneWriter);
    return ring_.has_value() && !lost() && !ring_->drained();
}

void OcramLaneWriter::wait(Wait on) noexcept {
    TASTY_SEAT_BODY(OcramLaneWriter);
    if (!ring_) {
        waiting_ = Wait::None;
        return;
    }
    if (on == Wait::None || !line_open_) {
        waiting_ = on;
        return;
    }
    const ocram::RingIndex at = on == Wait::Room ? ring_->half_behind() : ring_->head();
    waiting_ = ring_->arm_mark(at) ? on : Wait::None;
}

void OcramLaneWriter::serve_line() noexcept {
    TASTY_SEAT_BODY(OcramLaneWriter);
    if (!line_open_ || !line_.rang())
        return;
    line_.drain_ring(true, [this]() noexcept {
        if (ring_)
            ring_->disarm_mark();
    });
    counts_.rings.add();
    waiting_ = Wait::None;
}

void OcramLaneWriter::close_window() noexcept {
    TASTY_SEAT_BODY(OcramLaneWriter);
    if (ring_)
        ring_->disarm_mark();
    ring_.reset();
    waiting_ = Wait::None;
    pushed_at_.reset();
}

bool OcramLaneWriter::publish_(std::span<const hal::LinkWord> words) noexcept {
    if (!ring_ || words.empty())
        return false;
    const ocram::RecordHeader header = ocram::RecordHeader::lframe_for(words);
    std::vector<std::uint32_t> payload(words.size());
    const std::span<std::uint32_t> packed = header.len().prefix_of(std::span{payload});
    ocram::RecordHeader::pack_lframe(words, packed);
    if (!ring_->publish(header, packed))
        return false;
    if (!pushed_at_ && wiring_.inbox)
        pushed_at_ = wiring_.inbox->pushes();
    counts_.frames.add();
    return true;
}

Ex<void> OcramLaneWriter::Frame::begin(proto::LinkSel sel, hal::LinkWord op) {
    TASTY_SEAT_BODY(OcramLaneWriter);
    words_.clear();
    open_ = false;

    if (sel != proto::LinkSel::Fpga) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    words_.push_back(op);
    open_ = true;
    return {};
}

Ex<void> OcramLaneWriter::Frame::word(hal::LinkWord w) {
    TASTY_SEAT_BODY(OcramLaneWriter);
    if (!open_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    words_.push_back(w);
    return {};
}

Ex<void> OcramLaneWriter::Frame::end() {
    TASTY_SEAT_BODY(OcramLaneWriter);
    if (!open_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    open_ = false;
    if (!owner_.publish_(words_))
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    return {};
}

}
