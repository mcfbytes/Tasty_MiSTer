// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "hal/axi.h"
#include "hal/ocram_hello_state.h"
#include "hal/ocram_lane_shape.h"
#include "hal/ocram_mailbox_slot.h"
#include "hal/ocram_ring_desc.h"
#include "hal/ocram_sector_shape.h"
#include "hal/ocram_types.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"

namespace mister::hal::ocram {

class ControlPage {
public:
    ControlPage() = default;

    explicit ControlPage(volatile std::byte* base) noexcept : base_(base) {}

    [[nodiscard]] bool bound() const noexcept { return base_ != nullptr; }

    void zero() const noexcept {
        for (infra::ByteOffset at = control::kBase; at < control::kSpanEnd;
             at += control::kStoreBytes) {
            detail::regs_write64(base_, at.bytes(), 0u);
        }
    }

    [[nodiscard]] HelloState h_hello() const noexcept {
        return HelloState::of(read(control::kHHello));
    }

    void store_h_hello(HelloState hello) const noexcept { write(control::kHHello, hello.word()); }

    [[nodiscard]] HelloState c_hello() const noexcept {
        return HelloState::of(read(control::kCHello));
    }

    void clear_c_hello() const noexcept { write(control::kCHello, 0u); }

    [[nodiscard]] BindSet h_bind() const noexcept { return BindSet::of(read(control::kHBind)); }

    void store_h_bind(BindSet binds) const noexcept { write(control::kHBind, binds.word()); }

    [[nodiscard]] BindSet c_bind() const noexcept { return BindSet::of(read(control::kCBind)); }

    [[nodiscard]] OcramCaps c_caps() const noexcept { return OcramCaps::of(read(control::kCCaps)); }

    [[nodiscard]] LayoutVersion c_version() const noexcept {
        return LayoutVersion::of(read(control::kCVersion));
    }

    [[nodiscard]] bool faulted(Ring r) const noexcept {
        const std::uint32_t bit = fault_bit(r);
        return bit != 0u && (read(control::kCState) & bit) != 0u;
    }

    [[nodiscard]] std::optional<RingDesc> c_ring(Ring r) const noexcept {
        const std::optional<PerRingWord> word = PerRingWord::of(r);
        if (!word)
            return std::nullopt;
        const std::optional<RingDesc> desc = RingDesc::of(read(word->at(control::kCRing)));
        if (!desc)
            return std::nullopt;
        return desc->placed(r);
    }

    [[nodiscard]] std::optional<ImageRows> c_k_osd_rows() const noexcept {
        const std::uint32_t word = read(control::kCK + control::kOsdKWord);
        if (word == 0u)
            return std::nullopt;
        return ImageRows::from_high_half(word);
    }

    [[nodiscard]] std::optional<ImageRowBytes> c_k_osd_row_bytes() const noexcept {
        const std::uint32_t word = read(control::kCK + control::kOsdKWord);
        if (word == 0u)
            return std::nullopt;
        return ImageRowBytes::from_low_half(word);
    }

    [[nodiscard]] std::optional<SectorShape> c_k_sector() const noexcept {
        if (!SectorShape::one_descriptor(read(control::kCK + control::kSectorK2Word))) {
            return std::nullopt;
        }
        return SectorShape::of(read(control::kCK + control::kSectorK4Word));
    }

    [[nodiscard]] std::optional<LaneShape> c_k_lane() const noexcept {
        return LaneShape::of(read(control::kCK + control::kLaneK9Word));
    }

    [[nodiscard]] std::optional<SectorShape> c_k_mount(MailboxSlot k) const noexcept {
        switch (k) {
            case MailboxSlot::Mount0:
                return SectorShape::of(read(control::kCK + control::kMount0KWord));
            case MailboxSlot::Mount1:
                return SectorShape::of(read(control::kCK + control::kMount1KWord));
            case MailboxSlot::Sector:
                break;
        }
        return std::nullopt;
    }

    [[nodiscard]] RingIndex h_idx(Ring r) const noexcept {
        return RingIndex::from_word(read(index_word_(control::kHIdx, r)));
    }

    void store_h_idx(Ring r, RingIndex at) const noexcept {
        write(index_word_(control::kHIdx, r), at.word());
    }

    [[nodiscard]] RingIndex c_idx(Ring r) const noexcept {
        return RingIndex::from_word(read(index_word_(control::kCIdx, r)));
    }

private:
    struct FaultBits {
        static constexpr unsigned kShift = 8;
        static constexpr std::uint32_t kPad = 1u << kShift;
        static constexpr std::uint32_t kLane = 1u << (kShift + 1);
        static constexpr std::uint32_t kOsd = 1u << (kShift + 2);
        static constexpr std::uint32_t kEvt = 1u << (kShift + 3);
        static constexpr std::uint32_t kSector = 1u << (kShift + 4);
    };

    [[nodiscard]] static constexpr std::uint32_t fault_bit(Ring r) noexcept {
        switch (r) {
            case Ring::Pad:
                return FaultBits::kPad;
            case Ring::Lane:
                return FaultBits::kLane;
            case Ring::Osd:
                return FaultBits::kOsd;
            case Ring::Evt:
                return FaultBits::kEvt;
            case Ring::Sector:
                return FaultBits::kSector;
            case Ring::kCount:
                break;
        }
        return 0u;
    }

    [[nodiscard]] static infra::ByteOffset index_word_(infra::ByteOffset base, Ring r) noexcept {
        const std::optional<PerRingWord> word = PerRingWord::of(r);
        return word ? word->at(base) : base;
    }

    [[nodiscard]] std::uint32_t read(infra::ByteOffset at) const noexcept {
        return detail::regs_read(base_, at.bytes());
    }

    void write(infra::ByteOffset at, std::uint32_t word) const noexcept {
        detail::regs_write(base_, at.bytes(), word);
    }

    volatile std::byte* base_ = nullptr;
};

static_assert(RingDesc::of(0u) == std::nullopt);
static_assert(
    PerRingWord::of(Ring::kCount) == std::nullopt,
    "the ring count names no ring and no control word"
);

}
