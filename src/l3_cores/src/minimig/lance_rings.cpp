// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/lance_rings.h"

#include <algorithm>

#include "cores/minimig/lance_bits.h"
#include "cores/minimig/lance_span.h"
#include "infra/span_cut.h"

namespace mister::cores::minimig {

namespace {

using Csr0 = LanceBits::Csr0;
using Mode = LanceBits::Mode;
using Tmd1 = LanceBits::Tmd1;
using Tmd3 = LanceBits::Tmd3;
using Rmd1 = LanceBits::Rmd1;

constexpr LanceSpan kFlags{2};
constexpr LanceSpan kCount{4};
constexpr LanceSpan kStatus{6};

}

bool LanceRings::starts_frame_(const LanceRing& ring) const noexcept {
    return ram_.ram_word(ring.descriptor() + kFlags).contains(Tmd1::kOwn.with(Tmd1::kStp));
}

bool LanceRings::find_start_() noexcept {
    LanceRing& ring = regs_.tx();

    ring.wrap();
    const LanceRingSlot from = ring.cursor;
    do {
        if (starts_frame_(ring))
            return true;
        ring.advance();
        ring.wrap();
    } while (ring.cursor != from);
    return false;
}

bool LanceRings::transmit() noexcept {
    if (!regs_.csr0().intersects(Csr0::kTxon))
        return false;
    LanceRing& ring = regs_.tx();
    if (!ring.walkable() || !find_start_())
        return false;

    LanceWord flags = ram_.ram_word(ring.descriptor() + kFlags);
    const bool add_fcs = flags.intersects(Tmd1::kAddFcs);
    tx_.clear();
    bool underrun = false;
    LanceAddr d{};
    LanceWord status{};
    for (;;) {
        ring.wrap();
        d = ring.descriptor();
        const LanceWord low = ram_.ram_word(d);
        flags = ram_.ram_word(d + kFlags);
        const LanceWord count = ram_.ram_word(d + kCount);
        status = ram_.ram_word(d + kStatus);
        if (!flags.intersects(Tmd1::kOwn)) {
            status = status.with(Tmd3::kBuff.with(Tmd3::kUflo));
            flags = flags.with(Tmd1::kErr);
            regs_.lower(Csr0::kTxon);
            underrun = true;
            ram_.put_ram_word(d + kFlags, flags);
            ram_.put_ram_word(d + kStatus, status);
            break;
        }
        flags = flags.without(Tmd1::kOwn);
        const infra::ByteCount size = std::min(
            count.negated_count().byte_count(),
            svc::NicFrame::kPayloadMax
        );
        if (tx_.size() + size > svc::NicFrame::kPayloadMax) {
            ram_.put_ram_word(d + kFlags, flags);
            ram_.put_ram_word(d + kStatus, status);
            break;
        }
        ram_.ram_read(
            LanceAddr::of_buffer({.low = low, .high = flags}).in_ram(),
            infra::affine::first(tx_.room(), size)
        );
        tx_.grow(size);
        if (flags.intersects(Tmd1::kEnp) && !regs_.loopback())
            tx_.pad_to(kMinFrame);
        ring.advance();
        if (flags.intersects(Tmd1::kEnp))
            break;
        ram_.put_ram_word(d + kFlags, flags);
        ram_.put_ram_word(d + kStatus, status);
    }

    if (underrun) {
        regs_.raise(Csr0::kTint);
        regs_.rethink();
        return false;
    }
    if (tx_.size() < kMinFrame && !regs_.loopback()) {
        status = status.with(Tmd3::kBuff.with(Tmd3::kUflo));
        flags = flags.with(Tmd1::kErr);
        regs_.lower(Csr0::kTxon);
        ram_.put_ram_word(d + kStatus, status);
        ram_.put_ram_word(d + kFlags, flags);
        regs_.raise(Csr0::kTint);
        regs_.rethink();
        return false;
    }

    const LanceWord mode = regs_.mode();
    bool collided = mode.intersects(Mode::kColl);

    if (!regs_.loopback()
        && !collided
        && tx_.view().size() >= svc::MacAddress::kBytes
        && regs_.station().matches(tx_.view())) {
        collided = true;
    }
    if (collided) {
        flags = flags.with(Tmd1::kErr);
        status = status.with(Tmd3::kRtry);
    }
    ram_.put_ram_word(d + kFlags, flags);
    ram_.put_ram_word(d + kStatus, status);
    bool sent = false;
    if (!collided) {
        if (regs_.loopback()) {
            receive(tx_.view());
        } else {
            if (mode.intersects(Mode::kDtcr) && !add_fcs)
                tx_.shrink(svc::NicFrame::kFcsBytes);
            munge_.munge(tx_.data());
            port_.send(tx_.view());
            sent_.add();
            sent = true;
        }
    }
    regs_.raise(Csr0::kTint);
    regs_.rethink();
    return sent;
}

bool LanceRings::accepted_(std::span<const std::byte> frame) const noexcept {
    const svc::MacAddress dst = svc::MacAddress::at(frame);
    if (regs_.promiscuous() || dst.is_broadcast())
        return true;
    if (dst.is_group())
        return regs_.filter().accepts(dst);
    return dst == regs_.station();
}

void LanceRings::receive(std::span<const std::byte> frame) noexcept {
    if (!regs_.csr0().intersects(Csr0::kRxon)) {
        rx_off_.add();
        return;
    }
    if (infra::ByteCount{frame.size()} < kMinReceive)
        return;
    LanceRing& ring = regs_.rx();
    if (!ring.walkable())
        return;

    const bool loop = regs_.loopback();
    rx_.assign(frame);

    if (!loop) {
        munge_.munge(rx_.data());
        if (!accepted_(rx_.view()))
            return;
        const svc::MacAddress& me = regs_.station();
        const svc::MacAddress dst = svc::MacAddress::at(rx_.view());

        const bool from_me = me.matches(rx_.view().subspan(svc::MacAddress::kBytes));
        if (from_me && (dst == me || dst.is_broadcast()))
            return;
    }
    rx_.append_fcs();

    const infra::ByteCount len = rx_.size();
    infra::ByteCount filled{};
    bool first = true;
    for (;;) {
        ring.wrap();
        const LanceAddr d = ring.descriptor();
        const LanceWord low = ram_.ram_word(d);
        LanceWord flags = ram_.ram_word(d + kFlags);
        const LanceWord count = ram_.ram_word(d + kCount);
        LanceWord status = ram_.ram_word(d + kStatus);
        if (!flags.intersects(Rmd1::kOwn)) {
            ring_full_.add();
            if (!first) {
                flags = flags.with(Rmd1::kBuff.with(Rmd1::kOflo));
                regs_.lower(Csr0::kRxon);
            } else {
                regs_.raise(Csr0::kMiss);
            }
            ram_.put_ram_word(d + kFlags, flags);
            regs_.rethink();
            return;
        }
        flags = flags.without(Rmd1::kOwn);
        ring.advance();
        if (first) {
            flags = flags.with(Rmd1::kStp);
            first = false;
        }
        const infra::ByteCount size = count.negated_count().byte_count();
        const infra::ByteCount take = std::min(size, len - std::min(filled, len));
        ram_.ram_write(
            LanceAddr::of_buffer({.low = low, .high = flags}).in_ram(),
            infra::affine::first(infra::affine::from(rx_.view(), filled), take)
        );
        filled += size;
        if (filled >= len) {
            flags = flags.with(Rmd1::kEnp);
            status = LanceWord::of_length(len);
        }
        ram_.put_ram_word(d + kFlags, flags);
        ram_.put_ram_word(d + kStatus, status);
        if (filled >= len)
            break;
    }
    regs_.raise(Csr0::kRint);
    regs_.rethink();
    delivered_.add();
    if (loop)
        loopback_.add();
}

bool LanceRings::rx_has_space() noexcept {
    if (!regs_.csr0().intersects(Csr0::kRxon))
        return false;
    const LanceRing& ring = regs_.rx();

    if (!ring.walkable())
        return true;
    return ram_.ram_word(ring.descriptor() + kFlags).intersects(Rmd1::kOwn);
}

}
