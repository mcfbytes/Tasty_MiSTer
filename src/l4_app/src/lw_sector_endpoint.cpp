// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/lw_sector_endpoint.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

#include "cores/blank_save_spec.h"
#include "infra/byte_view.h"
#include "proto/sd_stat.h"
#include "reactor/cause_decode.h"
#include "reactor/notifier.h"

namespace mister::app {

svc::ISectorProgram::Source LwSectorEndpoint::answer_(proto::Lba lba) const noexcept {
    return grant_.program->answer(*disc_, lba);
}

void LwSectorEndpoint::write_lba_(Port& port, hal::SectorBlock::Reg reg, proto::Lba lba) noexcept {
    port.write(reg, lba.v);
}

LwSectorEndpoint::LwSectorEndpoint(const Wiring& w) noexcept : Base(w.grant), w_(w) {}

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

void LwSectorEndpoint::publish_hook_(const SectorRoute& r) noexcept {
    w_.route.publish(r);
}

void LwSectorEndpoint::prepare_hook_(const SectorGrant& g) noexcept {
    bind_cards_(g);
    if (!w_.port)
        block_.emplace(hal::reg::Block::redeem(g.block));
}

bool LwSectorEndpoint::arm_hook_(const SectorGrant& g) noexcept {
    if (line_.open(g.line, w_.doorbells))
        return true;
    store_ctrl_(0);
    return false;
}

void LwSectorEndpoint::bind_hook_(const SectorGrant&) noexcept {
    store_ctrl_(enables_() | hal::SectorBlock::kCtrlAbort);
}

void LwSectorEndpoint::bound_hook_() noexcept {
    recheck_ = true;
    for (const Card& c : cards_)
        if (c.is_bound())
            tally_.cards_on_lw = tally_.cards_on_lw.with(c.save_slot());
}

void LwSectorEndpoint::answer_hook_(std::uint16_t gen) noexcept {
    tally_.bind_gen = gen;
    publish_();
}

void LwSectorEndpoint::unbind_hook_() noexcept {
    line_.close();
    block_.reset();
    cards_ = {};
    recheck_ = false;
    for (Ahead& a : ahead_)
        a.live = false;
    ahead_done_ = kReadAhead;
    tally_.cards_on_lw = {};
}

bool LwSectorEndpoint::idle_bound_hook_() const noexcept {
    return !recheck_ && ahead_done_ >= kReadAhead;
}

void LwSectorEndpoint::take_geometry_() noexcept {
    const auto g = geom_rd_.take_if_changed(w_.geometry);
    if (!g)
        return;
    disc_.emplace(*g);
    for (Ahead& a : ahead_)
        a.live = false;
    ahead_done_ = kReadAhead;
}

void LwSectorEndpoint::bind_cards_(const SectorGrant& g) noexcept {
    constexpr std::uint32_t kWordBytes = hal::SectorBlock::kWordBytes;
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        const SectorCard& in = g.cards[i];
        cards_[i] = Card{};

        if (!w_.saves
            || in.frame_words == 0
            || in.image.len == 0
            || in.slot >= hal::SectorBlock::kCardSlots)
            continue;
        auto image = w_.image_backing.empty()
            ? hal::FpgaMemory::map(in.image, os::MmioRegion::Access::ReadOnly)
            : hal::FpgaMemory::borrow(w_.image_backing, in.image);
        if (!image) {
            ++tally_.card_fails;
            continue;
        }
        cards_[i] = Card{
            .slot = in.slot,
            .bytes = proto::BlockSize{static_cast<std::uint32_t>(in.frame_words) * kWordBytes},
            .image = std::move(*image)
        };
    }
}

std::uint32_t LwSectorEndpoint::enables_() const noexcept {
    std::uint32_t en = hal::SectorBlock::kCtrlEnable;
    for (const Card& c : cards_)
        if (c.is_bound())
            en |= hal::SectorBlock::ctrl_enable_slot(c.slot);
    return en;
}

void LwSectorEndpoint::store_ctrl_(std::uint32_t v) noexcept {
    port_().write(hal::SectorBlock::Reg::Ctrl, v);
    (void)port_().read(hal::SectorBlock::Reg::Ctrl);
}

LwSectorEndpoint::Port& LwSectorEndpoint::port_() noexcept {
    if (w_.port)
        return *w_.port;
    return *block_;
}

bool LwSectorEndpoint::cause_names_route_(std::uint32_t w) const noexcept {
    const reactor::CauseDecode d = reactor::decode_cause_checked(w);
    if (d.is_refused() || d.cause() != reactor::Cause::Blk)
        return false;
    const proto::SdStat st = proto::SdStat::of(proto::SdRegisterWord{w}.status());
    if (st.reads_one(grant_.program->slot()))
        return true;
    return st.valid && std::ranges::any_of(cards_, [&st](const Card& c) noexcept {
               return c.is_bound() && proto::SlotIndex::of(c.slot) == st.slot;
           });
}

std::optional<LwSectorEndpoint::Request> LwSectorEndpoint::read_request_() noexcept {
    using hal::SectorBlock;
    const std::uint32_t req = port_().read(SectorBlock::Reg::Req);
    if ((req & SectorBlock::kReqPending) == 0u)
        return std::nullopt;
    const proto::SdStat st = proto::SdStat::of(proto::SdRegisterWord{req}.status());
    if (st.reads_one(grant_.program->slot())) {
        return Request{.lba = proto::Lba{port_().read(SectorBlock::Reg::Lba)}};
    }
    if (!st.valid || !st.is_one_block() || st.rd == st.wr)
        return std::nullopt;
    for (const Card& c : cards_) {
        if (!c.is_bound() || c.save_slot() != st.slot || st.block_size != c.bytes)
            continue;
        return Request{
            .card = infra::OptRef<const Card>{c},
            .dir = st.wr ? BlockDir::Write : BlockDir::Read,
            .lba = proto::Lba{port_().read(SectorBlock::lba_slot(c.slot))}
        };
    }

    ++tally_.card_misses;
    return std::nullopt;
}

void LwSectorEndpoint::serve_bound_() noexcept {
    take_geometry_();
    const bool rang = line_.rang();
    if (!rang && !recheck_) {
        read_ahead_one_();
        return;
    }
    recheck_ = false;
    const auto t0 = clock_.now();
    if (!lease_())
        return;
    const std::uint32_t cause = line_.drain(rang, port_(), hal::SectorBlock::Reg::Cause);

    if ((rang || cause != 0u) && !cause_names_route_(cause)) {
        release_();
        ++tally_.spurious;
        publish_();
        return;
    }
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
        serve_card_(*q->card, q->dir, q->lba, t0);
        return;
    }
    const proto::Lba lba = q->lba;
    fill_frame_(lba);
    if (!lease_())
        return;
    if (store_frame_(
            hal::SectorBlock::Reg::Begin,
            lba,
            std::span<const std::byte>(frame_.data, frame_bytes_())
        )) {
        ++tally_.served;
    }
    peek_more_();
    release_();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(clock_.now() - t0);
    tally_.lat_max_us = std::max(tally_.lat_max_us, static_cast<std::uint32_t>(us.count()));
    publish_();
    ahead_from_ = lba + proto::BlockCount{1u};
    ahead_done_ = 0;
}

void LwSectorEndpoint::peek_more_() noexcept {
    if (std::ranges::none_of(cards_, [](const Card& c) noexcept { return c.is_bound(); }))
        return;
    if ((port_().read(hal::SectorBlock::Reg::Req) & hal::SectorBlock::kReqPending) != 0u) {
        recheck_ = true;
    }
}

std::uint32_t LwSectorEndpoint::frame_bytes_() const noexcept {
    constexpr std::uint32_t kWordBytes = hal::SectorBlock::kWordBytes;
    return std::min<std::uint32_t>(
        static_cast<std::uint32_t>(grant_.frame_words) * kWordBytes,
        static_cast<std::uint32_t>(svc::kDiscSlotBytes)
    );
}

void LwSectorEndpoint::fill_frame_(proto::Lba lba) noexcept {
    const std::uint32_t n = frame_bytes_();
    const svc::ISectorProgram::Source src = answer_(lba);
    if (!src.engine) {
        std::memset(frame_.data, src.fill, n);
        return;
    }
    if (take_ahead_(lba))
        return;
    read_engine_(frame_, lba);
    if (frame_.ok == 0 || frame_.got < n) {
        const std::size_t got = frame_.ok != 0 ? frame_.got : 0u;
        std::memset(frame_.data + got, src.fill, n - got);
    }
}

void LwSectorEndpoint::read_engine_(svc::DiscReadSlot& s, proto::Lba lba) noexcept {
    s.gen = svc::Generation{};
    s.lba = lba;
    s.form = grant_.program->form();
    s.want = static_cast<std::uint16_t>(svc::disc_form_bytes(s.form));
    s.got = 0;
    s.ok = false;
    w_.discs.serve_on_io(s);
}

bool LwSectorEndpoint::take_ahead_(proto::Lba lba) noexcept {
    for (Ahead& a : ahead_) {
        if (!a.live || a.slot.lba != lba)
            continue;
        a.live = false;
        if (!a.slot.ok)
            return false;
        std::memcpy(frame_.data, a.slot.data, a.slot.got);
        frame_.got = a.slot.got;
        frame_.ok = true;
        return frame_.got >= frame_bytes_();
    }
    return false;
}

void LwSectorEndpoint::read_ahead_one_() noexcept {
    while (ahead_done_ < kReadAhead) {
        const proto::BlockCount n{static_cast<std::uint32_t>(ahead_done_++)};
        const proto::Lba lba = ahead_from_ + n;
        if (!answer_(lba).engine)
            continue;
        const bool held = std::ranges::any_of(ahead_, [lba](const Ahead& a) noexcept {
            return a.live && a.slot.lba == lba;
        });
        if (held)
            continue;
        Ahead& a = ahead_[ahead_next_];
        ahead_next_ = (ahead_next_ + 1u) % kReadAhead;
        read_engine_(a.slot, lba);
        a.live = true;
        ++tally_.read_ahead;
        publish_();
        return;
    }
}

bool LwSectorEndpoint::store_frame_(
    hal::SectorBlock::Reg begin,
    proto::Lba lba,
    std::span<const std::byte> bytes
) noexcept {
    constexpr std::uint32_t kWordBytes = hal::SectorBlock::kWordBytes;
    constexpr unsigned kByteBits = 8;
    using hal::SectorBlock;
    Port& port = port_();
    const auto words = static_cast<std::uint32_t>(bytes.size() / kWordBytes);
    const auto word = [bytes](std::uint32_t i) noexcept {
        const std::byte* b = bytes.data() + kWordBytes * i;
        std::uint32_t w = 0;
        for (unsigned lane = 0; lane < kWordBytes; ++lane)
            w |= std::to_integer<std::uint32_t>(b[lane]) << (kByteBits * lane);
        return w;
    };
    write_lba_(port, begin, lba);
    std::uint32_t from = 0;
    std::uint32_t dropped = 0;
    SectorBlock::Accept last{};
    for (unsigned resend = 0;; ++resend) {
        for (std::uint32_t i = from; i < words; ++i)
            port.write(SectorBlock::data(i), word(i));
        const SectorBlock::Accept a = SectorBlock::accept(port.read(SectorBlock::Reg::Accept));
        if (a.drops > dropped) {
            tally_.drops += a.drops - dropped;
            dropped = a.drops;
        }
        if (a.ok) {
            read_counts_();
            return true;
        }
        if (resend == kMaxResends || a.first_drop >= words) {
            last = a;
            break;
        }
        ++tally_.resends;
        from = a.first_drop;
    }
    store_ctrl_(enables_() | SectorBlock::kCtrlAbort);
    if (last.first_drop < words) {
        ++tally_.corrupted;
    } else {
        ++tally_.begin_lost;
    }
    read_counts_();

    recheck_ = true;
    return false;
}

void LwSectorEndpoint::serve_card_(
    const Card& c,
    BlockDir dir,
    proto::Lba lba,
    os::RawMonotonicClock::time_point t0
) noexcept {
    if (dir == BlockDir::Read) {
        fill_card_(c, lba);
        if (!lease_())
            return;
        const bool whole = store_frame_(
            hal::SectorBlock::begin_slot(c.slot),
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

svc::ISaveFilesOnIo::Save LwSectorEndpoint::save_of_(const Card& c) noexcept {
    const svc::ISaveFilesOnIo::Save f = w_.saves->save_on_io(c.save_slot());
    SaveState& s = saves_[c.save_slot()];
    if (s.gen != f.gen) {
        s.gen = f.gen;
        const bool exists = f.extent.has_value();
        s.rule = proto::SaveSlotRule::at_mount(
            exists,
            f.extent.value_or(proto::FileSize{}),
            f.growable
        );
    }
    return f;
}

void LwSectorEndpoint::fill_card_(const Card& c, proto::Lba lba) noexcept {
    constexpr unsigned kEraseByte = 0xFF;
    const std::span<std::uint8_t> out = infra::byte_cast<std::uint8_t>(
        c.bytes.block_of(std::span<std::byte>{card_buf_})
    );
    const svc::ISaveFilesOnIo::Save f = save_of_(c);
    const proto::SaveSlotRule& rule = saves_[c.save_slot()].rule;
    if (f.file && !rule.deferred_create) {

        const auto n = f.file->read_at(c.offset_of(lba), out);
        if (n && *n > 0) {
            const std::size_t got = std::min(*n, out.size());
            std::memset(out.data() + got, 0, out.size() - got);
            return;
        }
    }
    ++tally_.card_blanks;
    if (!rule.deferred_create) {
        std::memset(out.data(), 0, out.size());
    } else if (grant_.blank && grant_.blank->declared()) {
        const std::uint64_t key = grant_.blank->lba_bytes.position_of(lba);
        cores::render_blank(*grant_.blank, key, out);
    } else {
        std::memset(out.data(), kEraseByte, out.size());
    }
}

bool LwSectorEndpoint::copy_image_(const Card& c, proto::Lba lba) noexcept {
    const std::uint64_t at = c.offset_of(lba);
    if (at > c.image->region().len)
        return false;
    const std::span<std::byte> block = c.bytes.block_of(std::span<std::byte>{card_buf_});
    const auto n = c.image->read_at(static_cast<std::size_t>(at), block);
    return n && *n == block.size();
}

bool LwSectorEndpoint::store_wack_(const Card& c, proto::Lba lba) noexcept {
    using hal::SectorBlock;
    for (unsigned attempt = 0; attempt <= kMaxResends; ++attempt) {
        write_lba_(port_(), SectorBlock::wack_slot(c.slot), lba);
        const SectorBlock::Accept a = SectorBlock::accept(port_().read(SectorBlock::Reg::Accept));
        if (a.drops != 0)
            tally_.drops += a.drops;
        if (a.ok)
            return true;
        ++tally_.resends;
    }
    recheck_ = true;
    return false;
}

void LwSectorEndpoint::persist_card_(const Card& c, proto::Lba lba) noexcept {
    const svc::ISaveFilesOnIo::Save f = save_of_(c);
    proto::SaveSlotRule& rule = saves_[c.save_slot()].rule;
    const std::span<const std::uint8_t> src = infra::byte_cast<const std::uint8_t>(
        c.bytes.block_of(std::span<const std::byte>{card_buf_})
    );
    const proto::SaveSlotRule::Plan plan = rule.plan_write(lba, c.bytes, c.bytes.v);
    if (!f.file) {
        ++tally_.card_fails;
        publish_();
        return;
    }

    switch (plan.act) {
        case proto::SaveSlotRule::Act::Discard:
            ++tally_.card_discards;
            break;
        case proto::SaveSlotRule::Act::Create:
            if (auto e = f.file->create(src.first(plan.len)); e) {
                rule.created(*e);
                ++tally_.card_creates;
            } else {
                ++tally_.card_fails;
            }
            break;
        case proto::SaveSlotRule::Act::Write:
            if (auto n = f.file->write_at(plan.offset, src.first(plan.len)); n && *n == plan.len) {
                rule.wrote(plan.offset, plan.len);
            } else {
                ++tally_.card_fails;
            }
            break;
    }
    publish_();
}

void LwSectorEndpoint::read_counts_() noexcept {
    const hal::SectorBlock::Counts c = hal::SectorBlock::counts(
        port_().read(hal::SectorBlock::Reg::Counts)
    );
    tally_.fab_rejected = c.rejected;
    tally_.fab_skipped = c.skipped;
    tally_.fab_aborted = c.aborted;
    tally_.fab_reset_fills = c.reset_fills;
}

}
