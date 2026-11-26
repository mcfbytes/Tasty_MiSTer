// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

#include "hal/axi.h"
#include "hal/ocram_line_set.h"
#include "hal/ocram_record.h"
#include "hal/ocram_ring_desc.h"
#include "hal/ocram_ring_lend.h"
#include "hal/ocram_types.h"
#include "hal/reg_word.h"
#include "infra/affine.h"
#include "infra/byte_offset.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"

namespace mister::hal::ocram {

template <class Owner>
class RingReader {
    TASTY_SEAT_FROM(Owner);

public:
    struct Record {
        std::optional<RecordHeader> header{};
        RegWord lead{};
        RingIndex at{};
    };

    RingReader() = default;
    RingReader(RingReader&&) noexcept = default;
    RingReader& operator=(RingReader&&) noexcept = default;
    RingReader(const RingReader&) = delete;
    RingReader& operator=(const RingReader&) = delete;
    ~RingReader() = default;

    [[nodiscard]] static RingReader redeem(RingLend lend) noexcept {
        RingReader r;
        if (!lend.lent())
            return r;
        r.lend_ = lend;
        r.tail_ = RingIndex::from_word(r.read(r.index_word_(control::kHIdx)));
        r.head_ = r.tail_;
        return r;
    }

    [[nodiscard]] bool lent() const noexcept { return lend_.lent(); }

    [[nodiscard]] bool notices_bound() const noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        if (!lend_.lent())
            return false;
        return BindSet::of(read(control::kCBind)).ring(Ring::Evt);
    }

    [[nodiscard]] bool pending() noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        if (!lend_.lent())
            return false;
        head_ = RingIndex::from_word(read(index_word_(control::kCIdx)));
        return head_ != tail_;
    }

    [[nodiscard]] std::optional<Record> next() noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        if (!lend_.lent())
            return std::nullopt;
        const Words size = lend_.desc().words();
        const Words waiting = head_ - tail_;
        if (waiting == Words{} || size < waiting)
            return std::nullopt;
        const std::uint32_t word = read_ring(tail_);
        const Words len = RecordHeader::span_of(word);
        if (waiting < Words{1} + len)
            return std::nullopt;
        Record rec{.header = RecordHeader::parse(word), .at = tail_};
        if (Words{} < len)
            rec.lead = RegWord{read_ring(tail_ + Words{1})};
        tail_ += Words{1} + len;
        return rec;
    }

    [[nodiscard]] std::span<std::uint32_t> read_payload(
        const Record& rec,
        std::span<std::uint32_t> out
    ) const noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        if (!lend_.lent() || !rec.header)
            return {};
        const std::span<std::uint32_t> got = rec.header->len().prefix_of(out);
        RingIndex at = rec.at + Words{1};
        for (std::uint32_t& w : got) {
            w = read_ring(at);
            at += Words{1};
        }
        return got;
    }

    void ack() noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        if (!lend_.lent())
            return;
        write(index_word_(control::kHIdx), tail_.word());
    }

    void enable_line(bool on) noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        if (!lend_.lent())
            return;
        const LineSet now = LineSet::of(read(control::kHLine));
        const Ring r = lend_.desc().ring();
        write(control::kHLine, (on ? now.with(r) : now.without(r)).word());
    }

    [[nodiscard]] RingIndex tail() const noexcept { return tail_; }

private:
    [[nodiscard]] infra::ByteOffset index_word_(infra::ByteOffset base) const noexcept {
        const std::optional<PerRingWord> word = PerRingWord::of(lend_.desc().ring());
        return word ? word->at(base) : base;
    }

    [[nodiscard]] std::uint32_t read(infra::ByteOffset at) const noexcept {
        return detail::regs_read(lend_.base_, at.bytes());
    }

    [[nodiscard]] std::uint32_t read_ring(RingIndex at) const noexcept {
        return read(lend_.desc().offset_of(at.slot_in(lend_.desc().words())));
    }

    void write(infra::ByteOffset at, std::uint32_t word) noexcept {
        detail::regs_write(lend_.base_, at.bytes(), word);
    }

    RingLend lend_{};
    RingIndex tail_{};
    RingIndex head_{};
};

extern template class RingReader<seat_owner::Io>;
static_assert(std::is_nothrow_move_constructible_v<RingReader<seat_owner::Io>>);
static_assert(!std::is_copy_constructible_v<RingReader<seat_owner::Io>>);

}
