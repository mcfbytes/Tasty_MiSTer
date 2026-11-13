// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "infra/decimal_text.h"
#include "infra/json_out.h"
#include "os/types.h"

namespace mister::os {
struct MapLength;
}

namespace mister::hal {

struct SpiWord {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr SpiWord with_high_byte(std::uint8_t byte) const noexcept {
        return SpiWord{static_cast<std::uint16_t>(
            v | (static_cast<std::uint16_t>(byte) << std::numeric_limits<std::uint8_t>::digits)
        )};
    }

    [[nodiscard]] constexpr Error refused(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(SpiWord, SpiWord) = default;
};

struct SpiOutput {
    static constexpr std::uint32_t kData = 0x0000'FFFFu;

    std::uint32_t v = 0;

    [[nodiscard]] constexpr SpiOutput with(SpiOutput bits) const noexcept {
        return SpiOutput{v | bits.v};
    }

    [[nodiscard]] constexpr SpiOutput without(SpiOutput bits) const noexcept {
        return SpiOutput{v & ~bits.v};
    }

    [[nodiscard]] constexpr bool has(SpiOutput bits) const noexcept {
        return (v & bits.v) == bits.v;
    }

    [[nodiscard]] constexpr SpiOutput with_data(SpiWord word) const noexcept {
        return SpiOutput{(v & ~kData) | word.v};
    }

    [[nodiscard]] constexpr SpiWord data() const noexcept {
        return SpiWord{static_cast<std::uint16_t>(v & kData)};
    }

    friend constexpr bool operator==(SpiOutput, SpiOutput) = default;
};

struct ButtonMask {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr bool has(ButtonMask bit) const noexcept { return (v & bit.v) != 0u; }

    friend constexpr bool operator==(ButtonMask, ButtonMask) = default;
};

struct GpiField {
    std::uint8_t shift = 0;
    std::uint32_t mask = 0;
};

inline constexpr GpiField kGpiDataField{0, 0x0000'FFFFu};
inline constexpr GpiField kGpiButtonsField{29, 0x3};

struct GpiWord {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::uint32_t field(GpiField f) const noexcept {
        return (v >> f.shift) & f.mask;
    }

    [[nodiscard]] constexpr bool test(GpiField f) const noexcept {
        return (v & (f.mask << f.shift)) != 0u;
    }

    [[nodiscard]] constexpr SpiWord data() const noexcept {
        return SpiWord{static_cast<std::uint16_t>(field(kGpiDataField))};
    }

    [[nodiscard]] constexpr ButtonMask buttons() const noexcept {
        return ButtonMask{static_cast<std::uint16_t>(field(kGpiButtonsField))};
    }

    friend constexpr bool operator==(GpiWord, GpiWord) = default;
};

struct EventCount8 {
    std::uint8_t v = 0;

    friend constexpr void json_field(
        infra::JsonOut& o,
        infra::JsonName key,
        EventCount8 c
    ) noexcept {
        o.field(key, c.v);
    }

    friend constexpr bool operator==(EventCount8, EventCount8) = default;

private:
    [[nodiscard]] static constexpr EventCount8 of_lane(
        std::uint32_t word,
        unsigned shift
    ) noexcept {
        return EventCount8{static_cast<std::uint8_t>(word >> shift)};
    }
    friend struct SectorBlock;
};

[[nodiscard]] constexpr std::uint32_t detail_of(GpiWord w) noexcept {
    return w.v;
}

static_assert(sizeof(GpiWord) == 4);

struct LwOffset;

struct LwExtent {
    std::uint32_t v = 0;

    [[nodiscard]] static std::optional<LwExtent> of_length(os::MapLength len) noexcept;

    template <class Layout>
    [[nodiscard]] static constexpr LwExtent of_layout() noexcept {
        static_assert(Layout::kSize <= 0xFFFFFFFFull, "a layout larger than the 32-bit LW space");
        return LwExtent{static_cast<std::uint32_t>(Layout::kSize)};
    }

    [[nodiscard]] os::MapLength map_length() const noexcept;

    [[nodiscard]] constexpr bool covers(LwExtent want) const noexcept { return v >= want.v; }

    [[nodiscard]] constexpr bool contains(LwOffset rel) const noexcept;

    [[nodiscard]] constexpr LwOffset from_origin() const noexcept;
    friend constexpr bool operator==(LwExtent, LwExtent) = default;
    friend constexpr auto operator<=>(LwExtent, LwExtent) = default;
};

struct LwOffset {
    static constexpr std::uint32_t kDword = 4;

    std::uint32_t v = 0;

    [[nodiscard]] constexpr LwOffset plus(LwOffset rel) const noexcept {
        return LwOffset{v + rel.v};
    }

    [[nodiscard]] constexpr bool dword_aligned() const noexcept {
        return (v & (kDword - 1u)) == 0u;
    }

    [[nodiscard]] constexpr bool fits(LwExtent window, LwExtent want) const noexcept {
        return want.v != 0 && want.v <= window.v && v <= window.v - want.v;
    }

    [[nodiscard]] constexpr std::size_t dword_index() const noexcept { return v / kDword; }

    [[nodiscard]] constexpr std::uint32_t distance_to(LwOffset from) const noexcept {
        return v - from.v;
    }

    [[nodiscard]] constexpr LwOffset relative_to(LwOffset origin) const noexcept {
        return LwOffset{v - origin.v};
    }

    [[nodiscard]] constexpr Error refused(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(LwOffset, LwOffset) = default;
    friend constexpr auto operator<=>(LwOffset, LwOffset) = default;
};

constexpr bool LwExtent::contains(LwOffset rel) const noexcept {
    return rel.v < v;
}

constexpr LwOffset LwExtent::from_origin() const noexcept {
    return LwOffset{v};
}

struct FabricAddr {
    std::uint32_t v = 0;
    friend constexpr bool operator==(FabricAddr, FabricAddr) = default;
    friend constexpr auto operator<=>(FabricAddr, FabricAddr) = default;
};

struct CoreTypeByte {
    static constexpr std::uint8_t kDualSdram = 0xA8;

    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool dual_sdram() const noexcept { return v == kDualSdram; }

    friend constexpr bool operator==(CoreTypeByte, CoreTypeByte) = default;
};

static_assert(sizeof(CoreTypeByte) == 1);

enum class Width : std::uint8_t { Byte = 0, Word = 1 };

[[nodiscard]] constexpr std::size_t word_bytes(Width w) noexcept {
    return w == Width::Word ? sizeof(SpiWord::v) : 1u;
}

[[nodiscard]] constexpr std::size_t words(Width w, std::size_t bytes) noexcept {
    const std::size_t b = word_bytes(w);
    return (bytes + b - 1u) / b;
}

using SchedPolicy = os::SchedPolicy;

struct FrameSeq {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr FrameSeq next() const noexcept { return FrameSeq{v + 1u}; }

    [[nodiscard]] constexpr std::uint32_t word() const noexcept {
        const std::uint32_t w = v;
        return w;
    }

    friend constexpr bool operator==(FrameSeq, FrameSeq) = default;
    friend constexpr auto operator<=>(FrameSeq, FrameSeq) = default;
};

struct WordOffset {
    std::size_t v = 0;
    friend constexpr bool operator==(WordOffset, WordOffset) = default;
    friend constexpr auto operator<=>(WordOffset, WordOffset) = default;
};

struct IoVersion {
    std::uint8_t v = 0;
    friend constexpr bool operator==(IoVersion, IoVersion) = default;
};

struct HeaderCtr {
    static constexpr std::uint8_t kMask = 0x7;

    struct Lag {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr bool in_range(Lag lo, Lag hi) const noexcept {
            return v >= lo.v && v <= hi.v;
        }

        [[nodiscard]] constexpr bool at_least(Lag n) const noexcept { return v >= n.v; }

        [[nodiscard]] static constexpr Lag of_frames(std::uint32_t n) noexcept {
            return Lag{static_cast<std::uint8_t>(n & kMask)};
        }

        friend constexpr bool operator==(Lag, Lag) = default;
        friend constexpr auto operator<=>(Lag, Lag) = default;
    };

    static constexpr Lag kReach = Lag{kMask};

    std::uint8_t v = 0;

    [[nodiscard]] constexpr Lag lead_over(HeaderCtr older) const noexcept {
        return Lag{static_cast<std::uint8_t>((v - older.v) & kMask)};
    }

    [[nodiscard]] constexpr HeaderCtr plus(Lag n) const noexcept {
        return HeaderCtr{static_cast<std::uint8_t>((static_cast<std::uint32_t>(v) + n.v) & kMask)};
    }

    [[nodiscard]] constexpr HeaderCtr minus(Lag n) const noexcept {
        return HeaderCtr{static_cast<std::uint8_t>((static_cast<std::uint32_t>(v) - n.v) & kMask)};
    }

    [[nodiscard]] constexpr bool parity() const noexcept { return (v & 1u) != 0u; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(HeaderCtr, HeaderCtr) = default;
};

static_assert(sizeof(HeaderCtr) == 1);

}
