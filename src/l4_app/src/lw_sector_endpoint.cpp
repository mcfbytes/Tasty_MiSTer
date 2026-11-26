// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/lw_sector_endpoint.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "infra/prelude.h"
#include "infra/seat_owner_io.h"
#include "proto/sd_stat.h"

namespace mister::app {

void LwSectorEndpoint::write_lba_(Port& port, hal::SectorBlock::Reg reg, proto::Lba lba) noexcept {
    port.write(reg, lba.register_word());
}

LwSectorEndpoint::LwSectorEndpoint(const Wiring& w) noexcept
    : Base(w.grant, w.lease),
      w_(w),
      source_({.geometry = w.geometry, .discs = w.discs}),
      blocks_(w.saves) {}

void LwSectorEndpoint::serve() noexcept {
    TASTY_SEAT_BODY(LwSectorEndpoint);
    Base::serve();
}

bool LwSectorEndpoint::idle() const noexcept {
    TASTY_SEAT_BODY(LwSectorEndpoint);
    return Base::idle();
}

void LwSectorEndpoint::on_pause() noexcept {
    TASTY_SEAT_BODY(LwSectorEndpoint);
    Base::on_pause();
}

void LwSectorEndpoint::on_stop() noexcept {
    TASTY_SEAT_BODY(LwSectorEndpoint);
    Base::on_stop();
}

void LwSectorEndpoint::on_resume() noexcept {
    TASTY_SEAT_BODY(LwSectorEndpoint);
    Base::on_resume();
}

bool LwSectorEndpoint::accepts_hook_(const SectorGrant& g) const noexcept {
    return g.block.lent();
}

void LwSectorEndpoint::publish_hook_(const SectorRoute& r) noexcept {
    w_.route.publish(r);
}

void LwSectorEndpoint::prepare_hook_(const SectorGrant& g) noexcept {
    source_.bind(g.body);
    bind_cards_(g);
    if (!w_.port)
        block_.emplace(hal::reg::Block::redeem(g.block));
}

bool LwSectorEndpoint::arm_hook_(const SectorGrant& g) noexcept {
    return line_.open(g.body.line, w_.doorbells);
}

void LwSectorEndpoint::bind_hook_(const SectorGrant&) noexcept {
    store_ctrl_(hal::SectorBlock::Ctrl::word(enables_(), hal::SectorBlock::Abort{}));
}

void LwSectorEndpoint::bound_hook_() noexcept {
    recheck_ = true;
    for (const Card& c : cards_)
        if (c.is_bound())
            tally_.cards_on_lw = tally_.cards_on_lw.with(c.save_slot());
}

void LwSectorEndpoint::answer_hook_(proto::BuildGeneration gen) noexcept {
    tally_.bind_gen = gen;
    publish_();
}

void LwSectorEndpoint::unbind_hook_() noexcept {
    line_.close();
    block_.reset();
    cards_ = {};
    recheck_ = false;
    source_.unbind();
    tally_.cards_on_lw = {};
}

bool LwSectorEndpoint::idle_bound_hook_() const noexcept {
    return !recheck_ && source_.ahead_idle();
}

void LwSectorEndpoint::bind_cards_(const SectorGrant& g) noexcept {
    constexpr std::uint32_t kWordBytes = hal::SectorBlock::kWordBytes;
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        const SectorCard& in = g.body.cards[i];
        cards_[i] = Card{};
        if (!w_.saves || in.frame_words == 0 || !in.image)
            continue;
        auto image = w_.image_backing.empty()
            ? hal::FpgaMemory::map(*in.image, os::MmioRegion::Access::ReadOnly)
            : hal::FpgaMemory::borrow(w_.image_backing, in.image->region());
        if (!image) {
            ++tally_.card_fails;
            continue;
        }
        cards_[i] = Card{
            .index = in.index,
            .slot = in.slot,
            .bytes = proto::BlockSize{static_cast<std::uint32_t>(in.frame_words) * kWordBytes},
            .image = std::move(*image)
        };
    }
}

hal::SectorBlock::Enables LwSectorEndpoint::enables_() const noexcept {
    hal::SectorBlock::Enables en = hal::SectorBlock::Slot{}.enable();
    for (const Card& c : cards_)
        if (c.is_bound())
            en = en.with(c.index);
    return en;
}

void LwSectorEndpoint::store_ctrl_(hal::RegWord v) noexcept {
    port_().write(hal::SectorBlock::Reg::Ctrl, v);
    (void)port_().read(hal::SectorBlock::Reg::Ctrl);
}

LwSectorEndpoint::Port& LwSectorEndpoint::port_() noexcept {
    if (w_.port)
        return *w_.port;
    return *block_;
}

std::optional<LwSectorEndpoint::Request> LwSectorEndpoint::read_request_() noexcept {
    using hal::SectorBlock;
    const SectorBlock::Request
        req = port_().read(SectorBlock::Reg::Req).decoded(SectorBlock::Request::of);
    if (!req.pending)
        return std::nullopt;
    if (!req.slot) {
        ++tally_.card_misses;
        return std::nullopt;
    }
    const SectorBlock::Slot k = *req.slot;
    const proto::SdStat st = proto::SdStat::of(proto::SdStatusWord::of(req.sdstat));
    const auto lba = [this, k]() noexcept {
        return proto::Lba::of(port_().read(k.regs().lba));
    };
    if (k.is_primary()) {

        if (!st.reads_one(grant_.body.program->slot()))
            return std::nullopt;
        return Request{.slot = k, .dir = BlockDir::Read, .lba = lba()};
    }
    for (const Card& c : cards_) {
        if (!c.is_bound() || c.index != k)
            continue;
        if (!st.valid || st.rd == st.wr || st.block_size != c.bytes)
            continue;
        return Request{
            .card = infra::OptRef<const Card>{c},
            .slot = k,
            .dir = st.wr ? BlockDir::Write : BlockDir::Read,
            .lba = lba()
        };
    }

    ++tally_.card_misses;
    return std::nullopt;
}

void LwSectorEndpoint::serve_bound_() noexcept {
    (void)source_.mount_changed();
    const bool rang = line_.rang();
    if (!rang && !recheck_) {
        read_ahead_one_();
        return;
    }
    recheck_ = false;
    const auto t0 = clock_.now();
    if (!lease_())
        return;

    (void)line_.drain(rang, port_(), hal::SectorBlock::Reg::Cause);
    const std::optional<Request> q = read_request_();
    release_();
    if (rang && !q) {
        ++tally_.spurious;
        publish_();
    }
    if (!q) {
        read_ahead_one_();
        return;
    }
    if (q->card) {
        serve_card_(*q->card, q->slot, q->dir, q->lba, t0);
        return;
    }
    const proto::Lba lba = q->lba;
    const std::span<const std::byte> frame = source_.frame_for(lba);
    if (!lease_())
        return;
    if (store_frame_(q->slot, lba, frame))
        ++tally_.served;
    peek_more_();
    release_();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(clock_.now() - t0);
    tally_.lat_max_us = std::max(tally_.lat_max_us, static_cast<std::uint32_t>(us.count()));
    publish_();
    source_.read_ahead_from(lba);
}

void LwSectorEndpoint::peek_more_() noexcept {
    if (port_().read(hal::SectorBlock::Reg::Req).decoded(hal::SectorBlock::Request::of).pending)
        recheck_ = true;
}

void LwSectorEndpoint::read_ahead_one_() noexcept {
    if (!source_.read_ahead_one())
        return;
    ++tally_.read_ahead;
    publish_();
}

bool LwSectorEndpoint::store_frame_(
    hal::SectorBlock::Slot slot,
    proto::Lba lba,
    std::span<const std::byte> bytes
) noexcept {
    constexpr std::uint32_t kWordBytes = hal::SectorBlock::kWordBytes;
    constexpr unsigned kByteBits = 8;
    using hal::SectorBlock;
    Port& port = port_();
    const SectorBlock::FrameIndex words{static_cast<std::uint16_t>(bytes.size() / kWordBytes)};
    const auto word = [bytes](SectorBlock::FrameIndex i) noexcept {
        const std::byte* b = i.bytes_at(bytes);
        std::uint32_t w = 0;
        for (unsigned lane = 0; lane < kWordBytes; ++lane)
            w |= std::to_integer<std::uint32_t>(b[lane]) << (kByteBits * lane);
        return w;
    };
    write_lba_(port, slot.regs().begin, lba);
    SectorBlock::FrameIndex from{};
    SectorBlock::Drops dropped{};
    SectorBlock::Accept last{};
    for (unsigned resend = 0;; ++resend) {
        for (SectorBlock::FrameIndex i = from; i < words; ++i)
            port.write(i.data_reg(), hal::RegWord{word(i)});
        const SectorBlock::Accept a = port.read(SectorBlock::Reg::Accept)
                                          .decoded(SectorBlock::Accept::of);
        if (a.drops > dropped) {
            tally_.drops = tally_.drops + a.drops.minus(dropped);
            dropped = a.drops;
        }
        if (a.ok)
            return true;
        if (resend == kMaxResends) {
            last = a;
            break;
        }
        ++tally_.resends;
        if (a.closed) {
            write_lba_(port, slot.regs().begin, lba);
            from = SectorBlock::FrameIndex{};
            dropped = SectorBlock::Drops{};
            continue;
        }
        from = a.next_index;
        if (!(from < words)) {
            last = a;
            break;
        }
    }
    store_ctrl_(SectorBlock::Ctrl::word(enables_(), SectorBlock::Abort{true}));
    if (slot.is_primary() && !last.closed) {
        ++tally_.corrupted;
    } else {
        ++tally_.begin_lost;
    }

    recheck_ = true;
    return false;
}

void LwSectorEndpoint::serve_card_(
    const Card& c,
    hal::SectorBlock::Slot slot,
    BlockDir dir,
    proto::Lba lba,
    os::RawMonotonicClock::time_point t0
) noexcept {
    if (dir == BlockDir::Read) {
        fill_card_(c, lba);
        if (!lease_())
            return;
        const bool whole = store_frame_(
            slot,
            lba,
            c.bytes.block_of(std::span<const std::byte>{card_buf_})
        );
        peek_more_();
        release_();
        if (whole)
            ++tally_.card_reads;
        note_card_latency_(t0);
        return;
    }
    if (!lease_())
        return;
    const bool sourced = copy_image_(c, lba);
    const bool acked = store_wack_(c, lba);
    peek_more_();
    release_();
    note_card_latency_(t0);
    if (!acked)
        return;
    ++tally_.card_writes;
    if (!sourced) {
        ++tally_.card_fails;
        publish_();
        return;
    }
    persist_card_(c, lba);
}

void LwSectorEndpoint::note_card_latency_(os::RawMonotonicClock::time_point t0) noexcept {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(clock_.now() - t0);
    tally_.card_lat_max_us = std::max(
        tally_.card_lat_max_us,
        static_cast<std::uint32_t>(us.count())
    );
    publish_();
}

void LwSectorEndpoint::fill_card_(const Card& c, proto::Lba lba) noexcept {
    const std::span<std::byte> out = c.bytes.block_of(std::span<std::byte>{card_buf_});
    if (blocks_.read_block(c.save_slot(), c.bytes, lba, out) == SaveSlotBlocks::Read::Blank)
        ++tally_.card_blanks;
}

bool LwSectorEndpoint::copy_image_(const Card& c, proto::Lba lba) noexcept {
    const proto::FileSize before = c.image_bytes_before(lba);
    if (before.file_word<std::uint64_t>() > c.image->region().len)
        return false;
    const std::span<std::byte> block = c.bytes.block_of(std::span<std::byte>{card_buf_});
    const auto n = c.image->read_at(before.file_word<std::size_t>(), block);
    return n && *n == block.size();
}

bool LwSectorEndpoint::store_wack_(const Card& c, proto::Lba lba) noexcept {
    using hal::SectorBlock;
    for (unsigned attempt = 0;; ++attempt) {
        write_lba_(port_(), c.index.regs().wack, lba);
        const SectorBlock::Accept
            a = port_().read(SectorBlock::Reg::Accept).decoded(SectorBlock::Accept::of);
        if (!a.drops.empty())
            tally_.drops = tally_.drops + a.drops;
        if (a.ok)
            return true;
        if (attempt == kMaxResends)
            break;
        ++tally_.resends;
    }
    recheck_ = true;
    return false;
}

void LwSectorEndpoint::persist_card_(const Card& c, proto::Lba lba) noexcept {
    const std::span<const std::byte> block = c.bytes.block_of(
        std::span<const std::byte>{card_buf_}
    );

    switch (blocks_.write_block(c.save_slot(), c.bytes, lba, block)) {
        case SaveSlotBlocks::Write::Wrote:
            break;
        case SaveSlotBlocks::Write::Created:
            ++tally_.card_creates;
            break;
        case SaveSlotBlocks::Write::Discarded:
            ++tally_.card_discards;
            break;
        case SaveSlotBlocks::Write::Failed:
        case SaveSlotBlocks::Write::NoFile:
            ++tally_.card_fails;
            break;
    }
    publish_();
}

template class GrantEndpoint<SectorGrant, SectorRoute, seat_owner::Io>;

}

namespace mister::hal {
template class WindowPort<SectorBlock, seat_owner::Io>;
}

namespace mister::infra::xthread {
template class GrantLease<app::SectorGrant, seat_owner::Io>;
}
