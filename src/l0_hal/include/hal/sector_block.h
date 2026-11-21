// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "hal/reg_access.h"
#include "hal/reg_field.h"
#include "hal/reg_word.h"
#include "infra/json_out.h"

namespace mister::hal {

struct SectorBlock {
    enum class Reg : std::uint32_t {
        Cause = 0x000,
        Req = 0x004,
        Ctrl = 0x008,
        Status = 0x00C,
        Accept = 0x010,
        Id = 0x014,
    };
    static constexpr std::size_t kSize = 0x8000;

    static constexpr RegAccess kAccess = RegAccess::ReadWrite;
    static constexpr std::uint32_t kId = 0x33434553u;

    static constexpr std::uint32_t kMaxFrameWords = 4096;

    static constexpr std::uint32_t kMaxSaveFrameWords = 510;

    enum Layout : std::uint32_t {
        kWordBytes = 4,
        kDataBase = 0x4000,
        kSlotBase = 0x100,
        kSlotStride = 0x20,

        kRstatWord = 0x0,
        kLbaWord = 0x4,
        kBeginWord = 0x8,
        kWackWord = 0xC,
    };

    static constexpr RegField<std::uint32_t> kAbortField{8, 0x1};

    struct Abort {
        bool v = false;

        [[nodiscard]] constexpr bool asserted() const noexcept { return v; }

        friend constexpr bool operator==(Abort, Abort) = default;
    };

    struct Enables;

    struct Slot {
        std::uint8_t v = 0;
        static constexpr std::uint32_t kCount = 3;

        template <std::integral I>
        [[nodiscard]] static constexpr Slot of(I i) noexcept {
            return Slot{static_cast<std::uint8_t>(i)};
        }

        [[nodiscard]] static constexpr std::optional<Slot> of_field(std::uint32_t k) noexcept {
            if (k >= kCount)
                return std::nullopt;
            return Slot{static_cast<std::uint8_t>(k)};
        }

        [[nodiscard]] constexpr bool is_primary() const noexcept { return v == 0; }

        [[nodiscard]] constexpr Enables enable() const noexcept;

        [[nodiscard]] constexpr RegWord cause_bit() const noexcept { return RegWord{1u << v}; }

        struct Regs {
            Reg rstat;
            Reg lba;
            Reg begin;
            Reg wack;
        };

        [[nodiscard]] constexpr Regs regs() const noexcept {
            return Regs{
                .rstat = reg_(kRstatWord),
                .lba = reg_(kLbaWord),
                .begin = reg_(kBeginWord),
                .wack = reg_(kWackWord)
            };
        }

        friend constexpr bool operator==(Slot, Slot) = default;
        friend constexpr auto operator<=>(Slot, Slot) = default;

    private:
        [[nodiscard]] constexpr Reg reg_(Layout in_stride) const noexcept {
            return static_cast<Reg>(kSlotBase + kSlotStride * v + in_stride);
        }
    };

    struct Enables {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr Enables with(Slot s) const noexcept {
            return Enables{static_cast<std::uint8_t>(v | s.enable().v)};
        }

        [[nodiscard]] constexpr bool has(Slot s) const noexcept { return (v & s.enable().v) != 0u; }

        [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

        [[nodiscard]] constexpr RegWord register_word(Abort abort) const noexcept {
            return RegWord{kAbortField.set(v, {abort.asserted() ? 1u : 0u})};
        }

        friend constexpr bool operator==(Enables, Enables) = default;
    };

    struct Ctrl {
        [[nodiscard]] static constexpr RegWord word(Enables enables, Abort abort) noexcept {
            return enables.register_word(abort);
        }
    };

    struct FrameIndex {
        std::uint16_t v = 0;

        [[nodiscard]] constexpr Reg data_reg() const noexcept {
            return static_cast<Reg>(kDataBase + kWordBytes * v);
        }

        [[nodiscard]] constexpr const std::byte* bytes_at(
            std::span<const std::byte> frame
        ) const noexcept {
            return frame.data() + kWordBytes * v;
        }

        constexpr FrameIndex& operator++() noexcept {
            ++v;
            return *this;
        }

        friend constexpr bool operator<(FrameIndex a, FrameIndex b) noexcept { return a.v < b.v; }

        friend constexpr bool operator==(FrameIndex, FrameIndex) = default;
    };

    struct Drops {
        std::uint16_t v = 0;

        [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

        friend constexpr Drops operator+(Drops total, Drops add) noexcept {
            return Drops{static_cast<std::uint16_t>(total.v + add.v)};
        }

        [[nodiscard]] constexpr Drops minus(Drops part) const noexcept {
            return Drops{static_cast<std::uint16_t>(v - part.v)};
        }

        friend constexpr bool operator==(Drops, Drops) = default;
        friend constexpr auto operator<=>(Drops, Drops) = default;

        friend constexpr void json_field(infra::JsonOut& o, infra::JsonName key, Drops d) noexcept {
            o.field(key, d.v);
        }
    };

    struct Request {
        bool pending = false;
        std::optional<Slot> slot{};
        RegWord sdstat{};
        static constexpr RegField<std::uint32_t> kPendingField{31, 0x1};
        static constexpr RegField<std::uint32_t> kSlotField{16, 0x3};
        static constexpr RegField<std::uint32_t> kSdstatField{0, 0xFFFF};

        [[nodiscard]] static constexpr Request of(std::uint32_t w) noexcept {
            return Request{
                .pending = kPendingField.test(w),
                .slot = Slot::of_field(kSlotField.get(w)),
                .sdstat = RegWord{kSdstatField.get(w)}
            };
        }
    };

    struct SlotStatus {
        bool offered = false;
        bool answered = false;
        RegWord sdstat{};
        static constexpr RegField<std::uint32_t> kOfferedField{31, 0x1};
        static constexpr RegField<std::uint32_t> kAnsweredField{30, 0x1};
        static constexpr RegField<std::uint32_t> kSdstatField{0, 0xFFFF};

        [[nodiscard]] static constexpr SlotStatus of(std::uint32_t w) noexcept {
            return SlotStatus{
                .offered = kOfferedField.test(w),
                .answered = kAnsweredField.test(w),
                .sdstat = RegWord{kSdstatField.get(w)}
            };
        }
    };

    struct Accept {
        bool ok = false;
        bool closed = false;
        Drops drops{};
        FrameIndex next_index{};
        static constexpr RegField<std::uint32_t> kOkField{31, 0x1};
        static constexpr RegField<std::uint32_t> kClosedField{30, 0x1};
        static constexpr RegField<std::uint32_t> kDropsField{16, 0xFFF};
        static constexpr RegField<std::uint32_t> kNextIndexField{0, 0x1FFF};

        [[nodiscard]] static constexpr Accept of(std::uint32_t w) noexcept {
            return Accept{
                .ok = kOkField.test(w),
                .closed = kClosedField.test(w),
                .drops = Drops{static_cast<std::uint16_t>(kDropsField.get(w))},
                .next_index = FrameIndex{static_cast<std::uint16_t>(kNextIndexField.get(w))}
            };
        }

        friend constexpr bool operator==(const Accept&, const Accept&) = default;
    };
};

constexpr SectorBlock::Enables SectorBlock::Slot::enable() const noexcept {
    return Enables{static_cast<std::uint8_t>(1u << v)};
}

static_assert(
    static_cast<std::uint32_t>(SectorBlock::Reg::Id) + 4u <= SectorBlock::kSize,
    "the register file ends inside the block"
);
static_assert(
    SectorBlock::kMaxFrameWords * SectorBlock::kWordBytes
        == SectorBlock::kSize - SectorBlock::kDataBase,
    "DATA spans the block above 0x4000"
);
static_assert(
    static_cast<std::uint32_t>(SectorBlock::Slot::of(2).regs().wack) == 0x14Cu,
    "slot 2's WACK_S is the last per-slot register word"
);
static_assert(
    static_cast<std::uint32_t>(SectorBlock::FrameIndex{SectorBlock::kMaxFrameWords - 1}.data_reg())
        == SectorBlock::kSize - SectorBlock::kWordBytes,
    "the last DATA word is the block's last word"
);
static_assert(
    SectorBlock::Ctrl::word(
        SectorBlock::Slot::of(0).enable().with(SectorBlock::Slot::of(2)),
        SectorBlock::Abort{true}
    ) == hal::RegWord{0x105u},
    "CTRL {enables by k, ABORT}"
);

}
