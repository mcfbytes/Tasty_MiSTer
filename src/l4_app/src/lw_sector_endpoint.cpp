// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/lw_sector_endpoint.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/epoll.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

#include "cores/blank_save_spec.h"
#include "infra/byte_view.h"
#include "infra/lease_order.h"
#include "infra/unique_fd.h"
#include "reactor/notifier.h"

namespace mister::app {

LwSectorEndpoint::LwSectorEndpoint(const Wiring& w) noexcept : w_(w) {}

LwSectorEndpoint::~LwSectorEndpoint() {
    if (line_.fd() >= 0 && w_.doorbells >= 0) {
        (void)::epoll_ctl(w_.doorbells, EPOLL_CTL_DEL, line_.fd(), nullptr);
    }
}

void LwSectorEndpoint::serve() noexcept {
    TASTY_SEAT_BODY(LwSectorEndpoint);
    take_grant_();
    take_geometry_();
    if (bound_gen_ == 0)
        return;
    service_request_();
}

bool LwSectorEndpoint::idle() const noexcept {
    TASTY_SEAT_BODY(LwSectorEndpoint);

    if (const auto g = w_.grant.generation(); g != 0 && g != grant_rd_.seen())
        return false;
    return bound_gen_ == 0 || (!recheck_ && ahead_done_ >= kReadAhead);
}

void LwSectorEndpoint::on_pause() noexcept {
    TASTY_SEAT_BODY(LwSectorEndpoint);
    unbind_();
}

void LwSectorEndpoint::on_stop() noexcept {
    TASTY_SEAT_BODY(LwSectorEndpoint);
    unbind_();
}

void LwSectorEndpoint::on_resume() noexcept {
    TASTY_SEAT_BODY(LwSectorEndpoint);
    grant_rd_.forget();
}

void LwSectorEndpoint::take_grant_() noexcept {
    const auto g = grant_rd_.take_if_changed(w_.grant);
    if (!g)
        return;

    unbind_();
    if (g->live())
        bind_(*g, grant_rd_.seen());
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

void LwSectorEndpoint::bind_(const SectorGrant& g, std::uint32_t gen) noexcept {
    grant_ = g;
    bound_gen_ = gen;
    bind_cards_(g);
    if (!w_.port)
        block_.emplace(hal::reg::Block::redeem(g.block));

    if (!lease_()) {
        answer_bind_(g.gen);
        return;
    }
    if (!arm_line_(g)) {
        store_ctrl_(0);
        release_();
        unbind_();
        ++tally_.bind_refusals;
        answer_bind_(g.gen);
        return;
    }

    store_ctrl_(enables_() | hal::SectorBlock::kCtrlAbort);
    release_();
    recheck_ = true;
    tally_.on_lw = 1;
    for (const Card& c : cards_)
        if (c.bytes != 0)
            tally_.cards_on_lw |= 1u << c.slot;
    answer_bind_(g.gen);
}

void LwSectorEndpoint::answer_bind_(std::uint16_t gen) noexcept {
    tally_.bind_gen = gen;
    publish_();
}

void LwSectorEndpoint::bind_cards_(const SectorGrant& g) noexcept {
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        const SectorCard& in = g.cards[i];
        cards_[i] = Card{};
        if (!w_.saves || in.frame_words == 0 || in.image.len == 0 || in.slot >= 4u)
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
            .bytes = static_cast<std::uint32_t>(in.frame_words) * 4u,
            .image = std::move(*image)
        };
    }
}

std::uint32_t LwSectorEndpoint::enables_() const noexcept {
    std::uint32_t en = hal::SectorBlock::kCtrlEnable;
    for (const Card& c : cards_)
        if (c.bytes != 0)
            en |= hal::SectorBlock::ctrl_enable_slot(c.slot);
    return en;
}

bool LwSectorEndpoint::arm_line_(const SectorGrant& g) noexcept {
    if (w_.doorbells < 0)
        return false;
    const int fd = ::fcntl(g.line.fd(), F_DUPFD_CLOEXEC, 0);
    auto line = os::UioHandle::adopt(g.line.line(), UniqueFd{fd});
    if (!line)
        return false;
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = line->fd();
    if (::epoll_ctl(w_.doorbells, EPOLL_CTL_ADD, line->fd(), &ev) != 0)
        return false;
    line_ = std::move(*line);
    (void)line_.borrow().consume();
    (void)line_.borrow().rearm();
    return true;
}

void LwSectorEndpoint::store_ctrl_(std::uint32_t v) noexcept {
    port_().write(hal::SectorBlock::Reg::Ctrl, v);
    (void)port_().read(hal::SectorBlock::Reg::Ctrl);
}

void LwSectorEndpoint::unbind_() noexcept {
    if (bound_gen_ == 0)
        return;
    if (line_.fd() >= 0 && w_.doorbells >= 0) {
        (void)::epoll_ctl(w_.doorbells, EPOLL_CTL_DEL, line_.fd(), nullptr);
    }
    line_ = os::UioHandle{};
    block_.reset();
    grant_ = SectorGrant{};
    cards_ = {};
    bound_gen_ = 0;
    recheck_ = false;
    for (Ahead& a : ahead_)
        a.live = false;
    ahead_done_ = kReadAhead;
    tally_.on_lw = 0;
    tally_.cards_on_lw = 0;
    publish_();
}

bool LwSectorEndpoint::lease_() noexcept {
    tally_.leased = 1;
    publish_();
    if (xthread::LeaseOrder::grant_unchanged(w_.grant, bound_gen_))
        return true;
    tally_.leased = 0;
    ++tally_.revoked;
    unbind_();
    return false;
}

void LwSectorEndpoint::release_() noexcept {
    tally_.leased = 0;
    publish_();
}

void LwSectorEndpoint::publish_() noexcept {
    w_.route.publish(tally_);
}

LwSectorEndpoint::Port& LwSectorEndpoint::port_() noexcept {
    if (w_.port)
        return *w_.port;
    return *block_;
}

bool LwSectorEndpoint::line_rang_() const noexcept {
    if (line_.fd() < 0)
        return false;
    pollfd p{line_.fd(), POLLIN, 0};
    return ::poll(&p, 1, 0) > 0 && (p.revents & POLLIN) != 0;
}

std::uint32_t LwSectorEndpoint::drain_cause_(bool rang) noexcept {
    if (rang)
        (void)line_.borrow().consume();
    const std::uint32_t w = port_().read(hal::SectorBlock::Reg::Cause);
    if (rang)
        (void)line_.borrow().rearm();
    return w;
}

bool LwSectorEndpoint::cause_names_route_(std::uint32_t w) const noexcept {
    const reactor::CauseDecode d = reactor::decode_cause_checked(w);
    if (d.refused || d.cause != reactor::Cause::Blk)
        return false;
    if (hal::SectorBlock::sdstat_reads(w, grant_.program->slot()))
        return true;
    const hal::SectorBlock::SdStat st = hal::SectorBlock::sdstat(w);
    return st.valid && std::ranges::any_of(cards_, [&st](const Card& c) noexcept {
               return c.bytes != 0 && c.slot == st.slot;
           });
}

LwSectorEndpoint::Request LwSectorEndpoint::read_request_() noexcept {
    using hal::SectorBlock;
    const std::uint32_t req = port_().read(SectorBlock::Reg::Req);
    if ((req & SectorBlock::kReqPending) == 0u)
        return {};
    if (SectorBlock::sdstat_reads(req, grant_.program->slot())) {
        return {.pending = true, .lba = port_().read(SectorBlock::Reg::Lba)};
    }
    const SectorBlock::SdStat st = SectorBlock::sdstat(req);
    if (!st.valid || st.count != 0 || st.rd == st.wr)
        return {};
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        const Card& c = cards_[i];
        if (c.bytes == 0 || c.slot != st.slot || (128u << st.blksz) != c.bytes)
            continue;
        return {
            .pending = true,
            .card = i,
            .write = st.wr,
            .lba = port_().read(SectorBlock::lba_slot(c.slot))
        };
    }
    return {};
}

void LwSectorEndpoint::service_request_() noexcept {
    const bool rang = line_rang_();
    if (!rang && !recheck_) {
        read_ahead_one_();
        return;
    }
    recheck_ = false;
    const auto t0 = clock_.now();
    if (!lease_())
        return;
    const std::uint32_t cause = drain_cause_(rang);

    if ((rang || cause != 0u) && !cause_names_route_(cause)) {
        release_();
        ++tally_.spurious;
        publish_();
        return;
    }
    const Request q = read_request_();
    release_();
    if (rang && !q.pending) {
        ++tally_.spurious;
        publish_();
    }
    if (!q.pending) {
        read_ahead_one_();
        return;
    }
    if (q.card) {
        serve_card_(cards_[*q.card], q.write, q.lba, t0);
        return;
    }
    const std::uint32_t lba = q.lba;
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
    ahead_from_ = lba + 1u;
    ahead_done_ = 0;
}

void LwSectorEndpoint::peek_more_() noexcept {
    if (std::ranges::none_of(cards_, [](const Card& c) noexcept { return c.bytes != 0; }))
        return;
    if ((port_().read(hal::SectorBlock::Reg::Req) & hal::SectorBlock::kReqPending) != 0u) {
        recheck_ = true;
    }
}

std::uint32_t LwSectorEndpoint::frame_bytes_() const noexcept {
    return std::min<std::uint32_t>(
        static_cast<std::uint32_t>(grant_.frame_words) * 4u,
        static_cast<std::uint32_t>(svc::kDiscSlotBytes)
    );
}

void LwSectorEndpoint::fill_frame_(std::uint32_t lba) noexcept {
    const std::uint32_t n = frame_bytes_();
    const svc::ISectorProgram::Source src = grant_.program->answer(*disc_, lba);
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

void LwSectorEndpoint::read_engine_(svc::DiscReadSlot& s, std::uint32_t lba) noexcept {
    s.gen = 0;
    s.lba = lba;
    s.form = grant_.program->form();
    s.want = static_cast<std::uint16_t>(svc::disc_form_bytes(s.form));
    s.got = 0;
    s.ok = 0;
    w_.discs.serve_on_io(s);
}

bool LwSectorEndpoint::take_ahead_(std::uint32_t lba) noexcept {
    for (Ahead& a : ahead_) {
        if (!a.live || a.slot.lba != lba)
            continue;
        a.live = false;
        if (a.slot.ok == 0)
            return false;
        std::memcpy(frame_.data, a.slot.data, a.slot.got);
        frame_.got = a.slot.got;
        frame_.ok = 1;
        return frame_.got >= frame_bytes_();
    }
    return false;
}

void LwSectorEndpoint::read_ahead_one_() noexcept {
    while (ahead_done_ < kReadAhead) {
        const std::uint32_t lba = ahead_from_ + static_cast<std::uint32_t>(ahead_done_++);
        if (!grant_.program->answer(*disc_, lba).engine)
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
    std::uint32_t lba,
    std::span<const std::byte> bytes
) noexcept {
    using hal::SectorBlock;
    Port& port = port_();
    const auto words = static_cast<std::uint32_t>(bytes.size() / 4u);
    const auto word = [bytes](std::uint32_t i) noexcept {
        const std::byte* b = bytes.data() + 4u * i;
        return std::to_integer<std::uint32_t>(b[0])
            | (std::to_integer<std::uint32_t>(b[1]) << 8)
            | (std::to_integer<std::uint32_t>(b[2]) << 16)
            | (std::to_integer<std::uint32_t>(b[3]) << 24);
    };
    port.write(begin, lba);
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
    bool write,
    std::uint32_t lba,
    std::chrono::nanoseconds t0
) noexcept {
    if (!write) {
        fill_card_(c, lba);
        if (!lease_())
            return;
        const bool whole = store_frame_(
            hal::SectorBlock::begin_slot(c.slot),
            lba,
            std::span<const std::byte>(card_buf_.data(), c.bytes)
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

void LwSectorEndpoint::note_card_latency_(std::chrono::nanoseconds t0) noexcept {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(clock_.now() - t0);
    tally_.card_lat_max_us = std::max(
        tally_.card_lat_max_us,
        static_cast<std::uint32_t>(us.count())
    );
    publish_();
}

svc::ISaveFilesOnIo::Save LwSectorEndpoint::save_of_(const Card& c) noexcept {
    const svc::ISaveFilesOnIo::Save f = w_.saves->save_on_io(proto::SlotIndex{c.slot});
    SaveState& s = saves_[c.slot & 3u];
    if (s.gen != f.gen) {
        s.gen = f.gen;
        s.rule = proto::SaveSlotRule::at_mount(f.exists, f.size, f.growable);
    }
    return f;
}

void LwSectorEndpoint::fill_card_(const Card& c, std::uint32_t lba) noexcept {
    const std::span<std::uint8_t> out = infra::byte_cast<std::uint8_t>(
        std::span<std::byte>(card_buf_.data(), c.bytes)
    );
    const svc::ISaveFilesOnIo::Save f = save_of_(c);
    const proto::SaveSlotRule& rule = saves_[c.slot & 3u].rule;
    const std::uint64_t at = static_cast<std::uint64_t>(lba) * c.bytes;
    if (f.file && !rule.deferred_create) {
        const auto n = f.file->read_at(at, out);
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
        const std::uint64_t key = static_cast<std::uint64_t>(lba) * grant_.blank->lba_bytes;
        cores::render_blank(*grant_.blank, key, out);
    } else {
        std::memset(out.data(), 0xFF, out.size());
    }
}

bool LwSectorEndpoint::copy_image_(const Card& c, std::uint32_t lba) noexcept {
    const std::uint64_t at = static_cast<std::uint64_t>(lba) * c.bytes;
    if (at > c.image->region().len)
        return false;
    const auto n = c.image->read_at(
        static_cast<std::size_t>(at),
        std::span<std::byte>(card_buf_.data(), c.bytes)
    );
    return n && *n == c.bytes;
}

bool LwSectorEndpoint::store_wack_(const Card& c, std::uint32_t lba) noexcept {
    using hal::SectorBlock;
    for (unsigned attempt = 0; attempt <= kMaxResends; ++attempt) {
        port_().write(SectorBlock::wack_slot(c.slot), lba);
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

void LwSectorEndpoint::persist_card_(const Card& c, std::uint32_t lba) noexcept {
    const svc::ISaveFilesOnIo::Save f = save_of_(c);
    proto::SaveSlotRule& rule = saves_[c.slot & 3u].rule;
    const std::span<const std::uint8_t> src = infra::byte_cast<const std::uint8_t>(
        std::span<const std::byte>(card_buf_.data(), c.bytes)
    );
    const proto::SaveSlotRule::Plan plan = rule.plan_write(proto::Lba{lba}, c.bytes, c.bytes);
    if (!f.file) {
        ++tally_.card_fails;
    } else if (plan.act == proto::SaveSlotRule::Act::Discard) {
        ++tally_.card_discards;
    } else if (plan.act == proto::SaveSlotRule::Act::Create) {
        if (auto e = f.file->create(src.first(plan.len)); e) {
            rule.created(*e);
            ++tally_.card_creates;
        } else {
            ++tally_.card_fails;
        }
    } else if (auto n = f.file->write_at(plan.offset, src.first(plan.len)); n && *n == plan.len) {
        rule.wrote(plan.offset, plan.len);
    } else {
        ++tally_.card_fails;
    }
    publish_();
}

void LwSectorEndpoint::read_counts_() noexcept {
    const std::uint32_t c = port_().read(hal::SectorBlock::Reg::Counts);
    tally_.fab_rejected = (c >> 24) & 0xFFu;
    tally_.fab_skipped = (c >> 16) & 0xFFu;
    tally_.fab_aborted = (c >> 8) & 0xFFu;
    tally_.fab_reset_fills = c & 0xFFu;
}

}
