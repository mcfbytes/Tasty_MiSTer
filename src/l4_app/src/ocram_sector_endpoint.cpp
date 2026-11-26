// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ocram_sector_endpoint.h"

#include <span>

#include "infra/endian.h"
#include "infra/prelude.h"
#include "infra/seat_owner_io.h"

namespace mister::app {

namespace ocram = hal::ocram;

namespace {

constexpr ocram::Words kFrameOverhead = ocram::Words{1} + ocram::RecordHeader::kLbaWords;

constexpr std::array<ocram::MailboxSlot, 3> kSlots{
    ocram::MailboxSlot::Sector,
    ocram::MailboxSlot::Mount0,
    ocram::MailboxSlot::Mount1
};
}

OcramSectorEndpoint::OcramSectorEndpoint(const Wiring& w) noexcept
    : Base(w.grant, w.lease),
      w_(w),
      source_({.geometry = w.geometry, .discs = w.discs}),
      blocks_(w.saves) {
    for (std::size_t i = 0; i < mounts_.size(); ++i)
        mounts_[i].k = ocram::kMountSlots[i];
}

void OcramSectorEndpoint::serve() noexcept {
    TASTY_SEAT_BODY(OcramSectorEndpoint);
    Base::serve();
}

bool OcramSectorEndpoint::idle() const noexcept {
    TASTY_SEAT_BODY(OcramSectorEndpoint);
    return Base::idle();
}

infra::ParkTimeout OcramSectorEndpoint::poll_period() const noexcept {
    TASTY_SEAT_BODY(OcramSectorEndpoint);
    return Base::bound() && phase_ == Phase::Draining && !bind_records_ ? kDrainPoll
                                                                        : infra::kForever;
}

void OcramSectorEndpoint::on_pause() noexcept {
    TASTY_SEAT_BODY(OcramSectorEndpoint);
    Base::on_pause();
}

void OcramSectorEndpoint::on_resume() noexcept {
    TASTY_SEAT_BODY(OcramSectorEndpoint);
    Base::on_resume();
}

void OcramSectorEndpoint::on_stop() noexcept {
    TASTY_SEAT_BODY(OcramSectorEndpoint);
    Base::on_stop();
}

infra::OptRef<OcramSectorEndpoint::Mount> OcramSectorEndpoint::mount_of_(
    ocram::MailboxSlot k
) noexcept {
    for (Mount& m : mounts_)
        if (m.k == k)
            return infra::OptRef<Mount>{m};
    return {};
}

bool OcramSectorEndpoint::accepts_hook_(const SectorGrant& g) const noexcept {
    return g.body.on_mailbox();
}

void OcramSectorEndpoint::publish_hook_(const SectorRoute& r) noexcept {
    w_.route.publish(r);
}

void OcramSectorEndpoint::prepare_hook_(const SectorGrant& g) noexcept {
    source_.bind(g.body);
    sector_.emplace(ocram::RingWriter<seat_owner::Io>::redeem(g.body.ring));
    evt_.emplace(ocram::RingReader<seat_owner::Io>::redeem(g.body.evt));
    frame_ = g.body.frame_words;
    ring_words_ = g.body.ring.desc().words();
    bind_records_ = g.body.bind_records;
    events_ = g.body.events && w_.events;
    bool any = false;
    for (std::size_t i = 0; i < mounts_.size(); ++i) {
        const MountSlot& in = g.body.mounts[i];
        const bool bind = blocks_.serves() && in.granted() && g.body.mount.lent();
        mounts_[i].grant = bind ? in : MountSlot{};
        mounts_[i].taken = Asks{};
        mounts_[i].wanted.reset();
        any = any || bind;
    }
    if (any)
        mount_ring_.emplace(ocram::RingWriter<seat_owner::Io>::redeem(g.body.mount));
}

bool OcramSectorEndpoint::arm_hook_(const SectorGrant& g) noexcept {
    return line_.open(g.body.line, w_.doorbells);
}

void OcramSectorEndpoint::bind_hook_(const SectorGrant& g) noexcept {
    if (events_)
        w_.events->bind(g.gen, g.body.conf_gen);
    evt_->enable_line(true);
    drop_ask_();
    seen_ = ocram::SlotBinds{};
    risen_ = ocram::SlotBinds{};
    seed_ = true;
}

void OcramSectorEndpoint::bound_hook_() noexcept {

    if (events_ && !evt_->notices_bound()) {
        w_.events->unbind();
        events_ = false;
    }
    tally_.notices = events_;
    tally_.bound = false;
    tally_.on_ocram = true;
    recheck_ = true;
    for (const Mount& m : mounts_)
        if (m.is_bound())
            tally_.cards_on_lw = tally_.cards_on_lw.with(m.grant.slot);
}

void OcramSectorEndpoint::answer_hook_(proto::BuildGeneration gen) noexcept {
    tally_.bind_gen = gen;
    publish_();
}

void OcramSectorEndpoint::unbind_hook_() noexcept {
    if (events_)
        w_.events->unbind();
    events_ = false;
    drop_ask_();
    if (evt_)
        evt_->enable_line(false);
    line_.close();
    sector_.reset();
    mount_ring_.reset();
    evt_.reset();
    source_.unbind();
    for (Mount& m : mounts_) {
        m.grant = MountSlot{};
        m.taken = Asks{};
        m.wanted.reset();
    }
    tally_.on_ocram = false;
    tally_.notices = false;
    tally_.cards_on_lw = {};
    recheck_ = false;
    bind_records_ = false;
    seed_ = false;
    seen_ = ocram::SlotBinds{};
    risen_ = ocram::SlotBinds{};
    ahead_left_ = proto::BlockCount{};
    wanted_.reset();
    next_.reset();
}

void OcramSectorEndpoint::store_ask_(ocram::SectorAsk ask) noexcept {
    if (sector_)
        sector_->ask(ask);
    ask_ = ask;
}

void OcramSectorEndpoint::drop_ask_() noexcept {
    store_ask_(ocram::SectorAsk{});
    phase_ = Phase::Draining;
}

ocram::SectorAsk OcramSectorEndpoint::wanted_ask_() const noexcept {
    ocram::SectorAsk ask = ocram::SectorAsk::asking(true);
    for (const Mount& m : mounts_)
        if (m.is_bound())
            ask = ask.with(m.k);
    return ask;
}

void OcramSectorEndpoint::lose_(infra::Tally& count) noexcept {
    count.add();
    release_();
    unbind_();
}

ocram::SlotBinds OcramSectorEndpoint::binds_view_() noexcept {
    return bind_records_ ? seen_ : sector_->slot_binds();
}

void OcramSectorEndpoint::serve_bound_() noexcept {
    if (!sector_ || !evt_ || !sector_->lent())
        return;
    if (!lease_())
        return;
    if (source_.mount_changed() && phase_ == Phase::Asking) {

        store_ask_(ask_.without(ocram::MailboxSlot::Sector));
        phase_ = Phase::Draining;
        tally_.rebinds.add();
    }

    if (!sector_->echoed())
        return lose_(tally_.ring_lost);
    if (sector_->faulted() || (mount_ring_ && mount_ring_->faulted()))
        return lose_(tally_.ring_faults);
    const bool rang = line_.rang();
    const ocram::SectorAsk want = wanted_ask_();

    const auto falls_in = [this, want](ocram::SlotBinds binds) noexcept {
        for (const ocram::MailboxSlot k : kSlots)
            if (want.asks(k) && !ask_.asks(k) && binds.has(k))
                return false;
        return true;
    };
    Drained d{};
    bool ready = false;
    bool drained = false;
    if (phase_ == Phase::Draining && !bind_records_) {

        ready = falls_in(sector_->slot_binds());
        if (rang || recheck_ || ready)
            drained = true;
    } else {
        drained = rang || recheck_;
    }
    if (drained) {
        recheck_ = false;
        d = drain_evt_(rang);
    }
    if (bind_records_ && seed_ && !d.write_lba) {
        seen_ = sector_->slot_binds();
        seed_ = false;
    }
    if (phase_ == Phase::Draining && bind_records_)
        ready = !seed_ && falls_in(seen_);

    const ocram::SlotBinds binds = binds_view_();
    judge_(d, binds);
    if (phase_ == Phase::Draining && ready && !d.write_lba) {
        for (const ocram::MailboxSlot k : kSlots)
            if (!ask_.asks(k))
                risen_ = risen_.without(k);
        wanted_.reset();
        next_.reset();
        ahead_left_ = proto::BlockCount{};
        recheck_ = false;
        store_ask_(want);
        phase_ = Phase::Asking;
        release_();
        return;
    }

    for (const ocram::MailboxSlot k : kSlots) {
        if (!ask_.asks(k))
            continue;
        if (binds.has(k)) {
            risen_ = risen_.with(k);
        } else if (risen_.has(k)) {
            return lose_(k == ocram::MailboxSlot::Sector ? tally_.ring_faults : tally_.mount_lost);
        }
    }
    if (d.write_lba) {
        release_();
        persist_write_(d);
        return;
    }
    for (Mount& m : mounts_) {
        if (!m.is_bound() || !m.wanted || !binds.has(m.k))
            continue;
        const Sent sent = publish_mount_(m);
        if (sent == Sent::Gone)
            return;
        if (sent == Sent::Full) {
            tally_.full.add();
        } else {
            if (sent == Sent::Stored)
                tally_.mount_frames.add();
            m.wanted.reset();
        }
        release_();
        return;
    }
    if (phase_ == Phase::Draining) {
        release_();
        return;
    }
    const bool bound = binds.has(ocram::MailboxSlot::Sector);
    if (bound && wanted_) {
        const Sent sent = publish_frame_(*wanted_);
        if (sent == Sent::Gone)
            return;
        if (sent == Sent::Stored) {
            ++tally_.served;
            next_ = *wanted_ + proto::BlockCount{1u};
            ahead_left_ = kQueueAhead;
            wanted_.reset();
        } else if (sent == Sent::Full) {
            tally_.full.add();
        }
    } else if (
        bound && next_ && ahead_left_ != proto::BlockCount{} && frames_waiting_() < kQueueAhead
    ) {
        const Sent sent = publish_frame_(*next_);
        if (sent == Sent::Gone)
            return;
        if (sent == Sent::Stored) {
            ++tally_.read_ahead;
            next_ = *next_ + proto::BlockCount{1u};
            ahead_left_ -= proto::BlockCount{1};
        } else if (sent == Sent::Full) {
            tally_.full.add();
        }
    }
    release_();
}

OcramSectorEndpoint::Drained OcramSectorEndpoint::drain_evt_(bool rang) noexcept {
    Drained d{};
    line_.drain_ring(rang, [this, &d]() noexcept {
        if (evt_->pending()) {
            while (const std::optional<Reader::Record> rec = evt_->next()) {
                take_record_(*rec, d);
                if (d.write_lba)
                    break;
            }
        }
        evt_->ack();
    });
    if (d.write_lba)
        recheck_ = true;
    return d;
}

void OcramSectorEndpoint::take_record_(const Reader::Record& rec, Drained& d) noexcept {
    if (!rec.header)
        return;
    const ocram::RecordHeader h = *rec.header;
    const std::optional<ocram::MailboxSlot> k = h.slot_arg().slot();
    switch (h.kind()) {
        case ocram::RecordHeader::Kind::Request: {
            if (!k)
                return;
            if (*k == ocram::MailboxSlot::Sector) {
                if (h.slot_arg() != ocram::SlotArg::primary())
                    return;
            } else if (!h.slot_arg().one_block()) {
                ++tally_.card_misses;
                return;
            }
            if (bind_records_ && !seed_ && !seen_.has(*k)) {
                d.voided.add();
                return;
            }
            const infra::OptRef<Mount> m = *k == ocram::MailboxSlot::Sector ? infra::OptRef<Mount>{}
                                                                            : mount_of_(*k);
            Asks& a = m ? m->taken : d.sector;
            a.latest = proto::Lba::of(rec.lead);
            a.count.add();
            return;
        }
        case ocram::RecordHeader::Kind::Write: {
            const infra::OptRef<Mount> m = k ? mount_of_(*k) : infra::OptRef<Mount>{};
            if (!m
                || !m->is_bound()
                || !h.slot_arg().one_block()
                || h.len() != ocram::RecordHeader::kLbaWords + m->grant.frame) {
                ++tally_.card_misses;
                return;
            }
            const std::span<const std::uint32_t> words = evt_->read_payload(rec, payload_);
            const proto::BlockSize bytes = m->grant.block_size();
            infra::store_le_run<std::uint32_t>(
                ocram::RecordHeader::kLbaWords.past_in(words),
                bytes.block_of(std::span<std::byte>{block_})
            );
            d.write_lba = proto::Lba::of(rec.lead);
            d.write_k = *k;
            return;
        }
        case ocram::RecordHeader::Kind::Bind: {

            const ocram::SlotBinds now = h.slot_binds();
            if (!now.has(ocram::MailboxSlot::Sector)) {
                d.voided.add(d.sector.count);
                d.sector = Asks{};
            }
            for (Mount& m : mounts_) {
                if (now.has(m.k))
                    continue;
                d.voided.add(m.taken.count);
                m.taken = Asks{};
            }
            seen_ = now;
            return;
        }

        case ocram::RecordHeader::Kind::Status:
            if (events_)
                w_.events->status(evt_->read_payload(rec, payload_));
            return;
        case ocram::RecordHeader::Kind::Info:
            if (const auto a = h.info_arg(); events_ && a)
                w_.events->info(*a);
            return;
        case ocram::RecordHeader::Kind::Upload:
            if (const auto a = h.upload_arg(); events_ && a)
                w_.events->upload(*a);
            return;
        case ocram::RecordHeader::Kind::Rumble:
            if (const auto a = h.rumble_arg(); events_ && a)
                w_.events->rumble(*a, evt_->read_payload(rec, payload_));
            return;
        case ocram::RecordHeader::Kind::Reply:
            return;
        case ocram::RecordHeader::Kind::Row:
        case ocram::RecordHeader::Kind::Frame:
        case ocram::RecordHeader::Kind::LFrame:
            return;
    }
}

void OcramSectorEndpoint::judge_(Drained& d, ocram::SlotBinds binds) noexcept {
    tally_.voided.add(d.voided);
    if (d.sector.count != infra::Tally{}) {
        if (phase_ == Phase::Asking && binds.has(ocram::MailboxSlot::Sector)) {
            tally_.requests.add(d.sector.count);
            wanted_ = d.sector.latest;
        } else {
            tally_.voided.add(d.sector.count);
        }
    }
    for (Mount& m : mounts_) {
        if (m.taken.count == infra::Tally{})
            continue;
        if (m.is_bound() && ask_.asks(m.k) && binds.has(m.k)) {
            tally_.mount_requests.add(m.taken.count);
            m.wanted = m.taken.latest;
        } else {
            tally_.voided.add(m.taken.count);
        }
        m.taken = Asks{};
    }
}

void OcramSectorEndpoint::persist_write_(const Drained& d) noexcept {
    const infra::OptRef<Mount> m = mount_of_(d.write_k);
    if (!m || !m->is_bound() || !d.write_lba)
        return;
    const proto::BlockSize bytes = m->grant.block_size();
    ++tally_.card_writes;

    switch (blocks_.write_block(
        m->grant.slot,
        bytes,
        *d.write_lba,
        bytes.block_of(std::span<const std::byte>{block_})
    )) {
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

bool OcramSectorEndpoint::retake_() noexcept {
    if (!lease_())
        return false;
    if (!sector_->echoed()) {
        lose_(tally_.ring_lost);
        return false;
    }
    if (sector_->faulted()) {
        lose_(tally_.ring_faults);
        return false;
    }
    return true;
}

std::span<const std::uint32_t> OcramSectorEndpoint::pack_(
    proto::Lba lba,
    std::span<const std::byte> bytes,
    ocram::Words words
) noexcept {
    const std::span<std::uint32_t> out = (ocram::RecordHeader::kLbaWords + words)
                                             .prefix_of<std::uint32_t>(payload_);
    out.front() = lba.register_word().bits();
    const std::span<std::uint32_t> body = out.subspan(1);
    for (std::size_t i = 0; i < body.size(); ++i) {
        const std::size_t at = ocram::Words::kWordBytes * i;
        body[i] = at + ocram::Words::kWordBytes <= bytes.size()
            ? infra::load_le<std::uint32_t>(bytes.subspan(at).first<ocram::Words::kWordBytes>())
            : 0u;
    }
    return out;
}

OcramSectorEndpoint::Sent OcramSectorEndpoint::publish_frame_(proto::Lba lba) noexcept {
    if (sector_->room() < kFrameOverhead + frame_)
        return Sent::Full;
    release_();
    const std::span<const std::byte> bytes = source_.frame_for(lba);
    if (!retake_())
        return Sent::Gone;
    if (phase_ != Phase::Asking || !sector_->slot_bound())
        return Sent::Skipped;
    return sector_->publish(
               ocram::RecordHeader::frame(ocram::SlotArg::primary(), frame_),
               pack_(lba, bytes, frame_)
           )
        ? Sent::Stored
        : Sent::Full;
}

OcramSectorEndpoint::Sent OcramSectorEndpoint::publish_mount_(Mount& m) noexcept {
    const ocram::Words words = m.grant.frame;
    if (mount_ring_->room() < kFrameOverhead + words)
        return Sent::Full;
    release_();
    const proto::BlockSize bytes = m.grant.block_size();
    const std::span<std::byte> out = bytes.block_of(std::span<std::byte>{block_});
    if (blocks_.read_block(m.grant.slot, bytes, *m.wanted, out) == SaveSlotBlocks::Read::Blank)
        ++tally_.card_blanks;
    if (!retake_())
        return Sent::Gone;
    if (!sector_->slot_binds().has(m.k))
        return Sent::Skipped;
    return mount_ring_->publish(
               ocram::RecordHeader::frame(m.grant.arg, words),
               pack_(*m.wanted, out, words)
           )
        ? Sent::Stored
        : Sent::Full;
}

proto::BlockCount OcramSectorEndpoint::frames_waiting_() const noexcept {
    const ocram::Words capacity = sector_->room();
    const ocram::Words used = ring_words_ < capacity ? ocram::Words{} : ring_words_ - capacity;
    return proto::BlockCount{used / (kFrameOverhead + frame_)};
}

bool OcramSectorEndpoint::idle_bound_hook_() const noexcept {
    for (const Mount& m : mounts_)
        if (m.wanted)
            return false;

    if (phase_ == Phase::Draining)
        return !recheck_;
    if (recheck_ || wanted_)
        return false;
    return !(risen_.has(ocram::MailboxSlot::Sector) && next_ && ahead_left_ != proto::BlockCount{});
}

}
