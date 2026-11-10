// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sched.h>

#include <array>
#include <compare>
#include <concepts>
#include <cstdint>
#include <string_view>

#include "infra/decimal_text.h"
#include "infra/error.h"
#include "infra/hex_text.h"
#include "infra/json_out.h"
#include "infra/seat.h"

namespace mister::os {

struct PhysAddr {
    std::uint64_t v = 0;

    [[nodiscard]] constexpr std::array<std::uint16_t, 2> low_halves() const noexcept {
        constexpr unsigned kHalfBits = 16;
        return {static_cast<std::uint16_t>(v), static_cast<std::uint16_t>(v >> kHalfBits)};
    }

    [[nodiscard]] constexpr PhysAddr advanced(std::uint32_t bytes) const noexcept {
        return PhysAddr{v + bytes};
    }

    [[nodiscard]] constexpr bool misaligned_to(std::size_t granule) const noexcept {
        return v % granule != 0;
    }

    [[nodiscard]] constexpr std::int64_t file_offset() const noexcept {
        return static_cast<std::int64_t>(v);
    }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite site) const noexcept {
        return Error{code, site, static_cast<std::uint32_t>(v)};
    }

    friend constexpr bool operator==(PhysAddr, PhysAddr) = default;
    friend constexpr auto operator<=>(PhysAddr, PhysAddr) = default;
};

[[nodiscard]] constexpr std::uint32_t low32(PhysAddr a) noexcept {
    return static_cast<std::uint32_t>(a.v);
}

[[nodiscard]] constexpr Error address_error(
    Errc code,
    infra::ErrorSite site,
    PhysAddr at
) noexcept {
    return at.refusal(code, site);
}

struct VtNumber {
    std::uint16_t v = 0;
    friend constexpr bool operator==(VtNumber, VtNumber) = default;
};

static_assert(sizeof(VtNumber) == 2);

struct FsMagic {
    std::uint32_t v = 0;
    friend constexpr bool operator==(FsMagic, FsMagic) = default;
};

static_assert(sizeof(FsMagic) == 4);

struct CpuSet {
    static constexpr unsigned kCpus = 64;
    std::uint64_t v = 0;

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr CpuSet intersect(CpuSet other) const noexcept {
        return CpuSet{v & other.v};
    }

    [[nodiscard]] constexpr CpuSet without(CpuSet other) const noexcept {
        return CpuSet{v & ~other.v};
    }

    [[nodiscard]] infra::HexText text() const noexcept { return infra::HexText{v}; }

    friend constexpr bool operator==(CpuSet, CpuSet) = default;
};

static_assert(sizeof(CpuSet) == 8);

inline constexpr std::uint8_t kCpuMaskGroupBits = 32;

struct CpuId {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr CpuSet as_set() const noexcept {
        return CpuSet{v < CpuSet::kCpus ? (std::uint64_t{1} << v) : 0u};
    }

    [[nodiscard]] constexpr CpuSet housekeeping_set() const noexcept {
        return CpuId{std::uint8_t{v == 0 ? std::uint8_t{1} : std::uint8_t{0}}}.as_set();
    }

    [[nodiscard]] constexpr CpuId next() const noexcept {
        return CpuId{static_cast<std::uint8_t>(v < kCpuMaskGroupBits ? v + 1 : kCpuMaskGroupBits)};
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    [[nodiscard]] constexpr bool below(std::size_t online) const noexcept { return v < online; }

    friend constexpr bool operator==(CpuId, CpuId) = default;
    friend constexpr auto operator<=>(CpuId, CpuId) = default;
};

static_assert(sizeof(CpuId) == 1);

#if CPU_SETSIZE > UINT8_MAX

static_assert(UINT8_MAX < CPU_SETSIZE, "a CpuId can never name a cpu past the libc cpu_set_t");

[[nodiscard]] inline cpu_set_t cpu_set_of(CpuId cpu) noexcept {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu.v, &set);
    return set;
}

[[nodiscard]] inline bool cpu_set_has(const cpu_set_t& set, CpuId cpu) noexcept {
    return CPU_ISSET(cpu.v, &set) != 0;
}
#else

[[nodiscard]] inline cpu_set_t cpu_set_of(CpuId cpu) noexcept {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (cpu.v < CPU_SETSIZE)
        CPU_SET(cpu.v, &set);
    return set;
}

[[nodiscard]] inline bool cpu_set_has(const cpu_set_t& set, CpuId cpu) noexcept {
    return cpu.v < CPU_SETSIZE && CPU_ISSET(cpu.v, &set) != 0;
}
#endif

enum class SchedPolicy : std::uint8_t { Fifo, Other };

[[nodiscard]] constexpr SchedPolicy sched_policy_of(int policy) noexcept {
    return policy == SCHED_FIFO ? SchedPolicy::Fifo : SchedPolicy::Other;
}

[[nodiscard]] constexpr int sched_policy_field(SchedPolicy policy) noexcept {
    return policy == SchedPolicy::Fifo ? SCHED_FIFO : 0;
}

constexpr void json_field(infra::JsonOut& o, infra::JsonName key, SchedPolicy policy) noexcept {
    o.field(key, sched_policy_field(policy));
}

enum class UioLine : std::uint8_t {};

[[nodiscard]] constexpr std::uint32_t to_ordinal(UioLine line) noexcept {
    return static_cast<std::uint32_t>(line);
}

[[nodiscard]] constexpr UioLine pool_line_at(
    std::uint32_t ordinal,
    std::uint32_t first_line
) noexcept {
    return static_cast<UioLine>(ordinal - first_line);
}

struct UioMinor {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr infra::DecimalText text() const noexcept {
        return infra::DecimalText{v};
    }

    friend constexpr bool operator==(UioMinor, UioMinor) = default;
};

static_assert(sizeof(UioMinor) == 4);

struct KernelIrq {
    std::uint32_t v = 0;
    friend constexpr bool operator==(KernelIrq, KernelIrq) = default;
};

struct CpuMask {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr CpuMask of(CpuId cpu) noexcept {
        return CpuMask{cpu.v < kCpuMaskGroupBits ? (1u << cpu.v) : 0u};
    }

    [[nodiscard]] static constexpr CpuMask below(CpuId cpu) noexcept {
        return CpuMask{cpu.v >= kCpuMaskGroupBits ? ~std::uint32_t{0} : ((1u << cpu.v) - 1u)};
    }

    [[nodiscard]] constexpr bool contains(CpuId cpu) const noexcept {
        return cpu.v < kCpuMaskGroupBits && (v & (1u << cpu.v)) != 0u;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool subset_of(CpuMask super) const noexcept {
        return (v & ~super.v) == 0u;
    }

    [[nodiscard]] constexpr bool intersects(CpuMask other) const noexcept {
        return (v & other.v) != 0u;
    }

    [[nodiscard]] constexpr CpuMask without(CpuMask drop) const noexcept {
        return CpuMask{v & ~drop.v};
    }

    friend constexpr bool operator==(CpuMask, CpuMask) = default;
};

struct I2cAddr {
    std::uint8_t v = 0;
    friend constexpr bool operator==(I2cAddr, I2cAddr) = default;
};

struct I2cReg {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] constexpr I2cReg plus(I n) const noexcept {
        return I2cReg{static_cast<std::uint8_t>(v + n)};
    }

    [[nodiscard]] constexpr std::uint32_t errno_detail(const Error& cause) const noexcept {
        return v | (cause.detail << kErrnoShift);
    }

    static constexpr unsigned kErrnoShift = 8;
    friend constexpr bool operator==(I2cReg, I2cReg) = default;
};

}
