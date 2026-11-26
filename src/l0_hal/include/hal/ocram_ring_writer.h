// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "hal/axi.h"
#include "hal/ocram_control_page.h"
#include "hal/ocram_hello_state.h"
#include "hal/ocram_mark_word.h"
#include "hal/ocram_ring_desc.h"
#include "hal/ocram_record.h"
#include "hal/ocram_ring_lend.h"
#include "hal/ocram_sector_ask.h"
#include "hal/ocram_slot_binds.h"
#include "hal/ocram_types.h"
#include "infra/affine.h"
#include "infra/byte_offset.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"
#include "infra/seat_owner_main.h"
#include "infra/seat_owner_ui.h"

namespace mister::hal::ocram {

template <class Owner>
class RingWriter {
    TASTY_SEAT_FROM(Owner);

public:
    RingWriter() = default;
    RingWriter(RingWriter&&) noexcept = default;
    RingWriter& operator=(RingWriter&&) noexcept = default;
    RingWriter(const RingWriter&) = delete;
    RingWriter& operator=(const RingWriter&) = delete;
    ~RingWriter() = default;

    [[nodiscard]] static RingWriter redeem(RingLend lend) noexcept {
        RingWriter w;
        if (!lend.lent())
            return w;
        w.lend_ = lend;
        w.head_ = RingIndex::from_word(w.read(w.index_word_(control::kHIdx)));
        return w;
    }

    [[nodiscard]] bool lent() const noexcept { return lend_.lent(); }

    [[nodiscard]] bool echoed() const noexcept {
        if (!lend_.lent())
            return false;
        return HelloState::of(read(control::kCHello)).echoes(HelloState::session(lend_.nonce()));
    }

    [[nodiscard]] bool bound() const noexcept {
        if (!lend_.lent())
            return false;
        return BindSet::of(read(control::kCBind)).ring(lend_.desc().ring());
    }

    [[nodiscard]] bool live() const noexcept { return echoed() && bound(); }

    void ask_slot(bool on) noexcept { ask(SectorAsk::asking(on)); }

    void ask(SectorAsk slots) noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        if (!holds_sector_())
            return;
        write(control::kHSector, slots.word());
    }

    [[nodiscard]] bool slot_bound() const noexcept { return slot_binds().has(MailboxSlot::Sector); }

    [[nodiscard]] SlotBinds slot_binds() const noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        if (!holds_sector_())
            return SlotBinds{};
        return BindSet::of(read(control::kCBind)).slots();
    }

    [[nodiscard]] bool faulted() const noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        if (!lend_.lent())
            return false;
        return ControlPage{lend_.base_}.faulted(lend_.desc().ring());
    }

    [[nodiscard]] Words room() const noexcept {
        if (!lend_.lent())
            return Words{};
        const Words size = lend_.desc().words();
        const Words waiting = head_ - tail_();
        if (size < waiting)
            return Words{};
        return size - waiting;
    }

    [[nodiscard]] bool drained() const noexcept { return lend_.lent() && head_ == tail_(); }

    [[nodiscard]] RingIndex head() const noexcept { return head_; }

    [[nodiscard]] Words size() const noexcept { return lend_.desc().words(); }

    [[nodiscard]] RingIndex half_behind() const noexcept { return head_ - size().half(); }

    [[nodiscard]] bool arm_mark(RingIndex at) noexcept { return store_mark_(MarkWord::at(at)); }

    void disarm_mark() noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        (void)store_mark_(MarkWord::unarmed());
    }

    [[nodiscard]] bool publish(
        RecordHeader header,
        std::span<const std::uint32_t> payload
    ) noexcept {
        if (!lend_.lent() || !header.len().matches(payload))
            return false;
        const Words need = Words{1} + header.len();
        if (room() < need)
            return false;
        const Words size = lend_.desc().words();
        write_ring(head_.slot_in(size), header.word());
        RingIndex at = head_ + Words{1};
        for (const std::uint32_t word : payload) {
            write_ring(at.slot_in(size), word);
            at += Words{1};
        }
        head_ = at;
        write(index_word_(control::kHIdx), head_.word());
        return true;
    }

private:
    [[nodiscard]] bool store_mark_(MarkWord m) noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        if (!holds_mark_())
            return false;
        write(index_word_(control::kHMark), m.word());
        return true;
    }

    [[nodiscard]] bool holds_mark_() const noexcept {
        return lend_.lent()
            && lend_.desc().kind() == RingDesc::Kind::Lane
            && lend_.desc().line().has_value();
    }

    [[nodiscard]] bool holds_sector_() const noexcept {
        return lend_.lent() && lend_.desc().ring() == Ring::Sector;
    }

    [[nodiscard]] infra::ByteOffset index_word_(infra::ByteOffset base) const noexcept {
        const std::optional<PerRingWord> word = PerRingWord::of(lend_.desc().ring());
        return word ? word->at(base) : base;
    }

    [[nodiscard]] RingIndex tail_() const noexcept {
        return RingIndex::from_word(read(index_word_(control::kCIdx)));
    }

    [[nodiscard]] std::uint32_t read(infra::ByteOffset at) const noexcept {
        return detail::regs_read(lend_.base_, at.bytes());
    }

    void write(infra::ByteOffset at, std::uint32_t word) noexcept {
        detail::regs_write(lend_.base_, at.bytes(), word);
    }

    void write_ring(Words slot, std::uint32_t word) noexcept {
        write(lend_.desc().offset_of(slot), word);
    }

    RingLend lend_{};
    RingIndex head_{};
};

extern template class RingWriter<seat_owner::Ui>;
extern template class RingWriter<seat_owner::Io>;
extern template class RingWriter<seat_owner::Main>;
extern template class RingWriter<seat_owner::RT>;
static_assert(std::is_nothrow_move_constructible_v<RingWriter<seat_owner::Ui>>);
static_assert(!std::is_copy_constructible_v<RingWriter<seat_owner::Io>>);

}
