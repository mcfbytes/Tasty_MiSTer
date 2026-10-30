// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/seat.h"

namespace mister::hal {

enum class SchedPolicy : std::uint8_t { Fifo, Other };

enum class SpawnKind : std::uint8_t { PromoteInPlace, Create };

enum class DdrMapping : std::uint8_t { None, Owner };

struct ThreadRole {
    TASTY_SEAT_EXEMPT(const_shared);
    SeatTag seat;
    int cpu;
    SchedPolicy policy;
    int prio;
    std::size_t stack_bytes;
    SpawnKind spawn;
    DdrMapping ddr = DdrMapping::None;
};

inline constexpr std::size_t kThreadSeats = kSeatTagCount - 2;
static_assert(static_cast<std::size_t>(SeatTag::Main) == kSeatTagCount - 1, "Main is the last tag");
using ThreadMap = std::array<ThreadRole, kThreadSeats>;

inline constexpr std::size_t kCommNameMax = 15;

inline constexpr std::size_t kCommRoleMax = 10;

struct CommName {
    std::array<char, kCommNameMax + 1> text{};
};

constexpr CommName comm_name(std::string_view prefix, std::string_view role) noexcept {
    CommName out;
    if (role.size() > kCommNameMax)
        role = role.substr(0, kCommNameMax);
    const std::size_t room = kCommNameMax - role.size();
    const std::size_t avail = room > 0 ? room - 1 : 0;
    const std::size_t keep = prefix.size() < avail ? prefix.size() : avail;
    std::size_t n = 0;
    for (std::size_t i = 0; i < keep; ++i)
        out.text[n++] = prefix[i];
    if (keep > 0)
        out.text[n++] = ':';
    for (const char c : role)
        out.text[n++] = c;
    return out;
}

inline constexpr std::size_t kFifoStackBytes = 256u * 1024u;

inline constexpr std::size_t kOtherStackBytes = 8u * 1024u * 1024u;

inline constexpr std::size_t kRecorderStackBytes = 256u * 1024u;

inline constexpr std::size_t kLauncherStackBytes = 256u * 1024u;

inline constexpr std::size_t kHdOsdStackBytes = 2u * 1024u * 1024u;

inline constexpr ThreadMap kDe10ThreadMap{{
    {.seat = SeatTag::RT,
     .cpu = 1,
     .policy = SchedPolicy::Fifo,
     .prio = 40,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Frame,
     .cpu = 1,
     .policy = SchedPolicy::Fifo,
     .prio = 35,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Input,
     .cpu = 0,
     .policy = SchedPolicy::Fifo,
     .prio = 25,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Pcm,
     .cpu = 0,
     .policy = SchedPolicy::Fifo,
     .prio = 22,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create,
     .ddr = DdrMapping::Owner},
    {.seat = SeatTag::Prefetch,
     .cpu = 0,
     .policy = SchedPolicy::Fifo,
     .prio = 20,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Diag,
     .cpu = 0,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kOtherStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Ui,
     .cpu = 0,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kOtherStackBytes,
     .spawn = SpawnKind::Create,
     .ddr = DdrMapping::Owner},
    {.seat = SeatTag::Io,
     .cpu = 0,
     .policy = SchedPolicy::Fifo,
     .prio = 23,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create,
     .ddr = DdrMapping::Owner},
    {.seat = SeatTag::Capture,
     .cpu = 0,
     .policy = SchedPolicy::Fifo,
     .prio = 21,
     .stack_bytes = kFifoStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Encode,
     .cpu = 0,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kRecorderStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::RecWrite,
     .cpu = 0,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kRecorderStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::Launcher,
     .cpu = 0,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kLauncherStackBytes,
     .spawn = SpawnKind::Create},
    {.seat = SeatTag::HdOsd,
     .cpu = 0,
     .policy = SchedPolicy::Other,
     .prio = 0,
     .stack_bytes = kHdOsdStackBytes,
     .spawn = SpawnKind::Create},
}};

constexpr bool has_row(SeatTag tag) noexcept {
    return tag != SeatTag::Unbound && tag != SeatTag::Main;
}

constexpr std::size_t row_index(SeatTag tag) noexcept {
    if (!has_row(tag))
        __builtin_trap();
    return static_cast<std::size_t>(tag) - 1;
}

constexpr const ThreadRole& seat_of(const ThreadMap& m, SeatTag tag) noexcept {
    return m[row_index(tag)];
}

constexpr std::size_t name_len(const char* s) noexcept {
    std::size_t n = 0;
    while (s[n] != '\0')
        ++n;
    return n;
}

constexpr bool well_formed(const ThreadMap& m) noexcept {
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (static_cast<std::size_t>(m[i].seat) != i + 1)
            return false;
    }
    return true;
}

constexpr bool rt_holds_highest_fifo_prio(const ThreadMap& m) noexcept {
    const ThreadRole& rt = seat_of(m, SeatTag::RT);
    if (rt.policy != SchedPolicy::Fifo)
        return false;
    for (const ThreadRole& r : m) {
        if (r.seat == SeatTag::RT)
            continue;
        if (r.policy == SchedPolicy::Fifo && r.prio >= rt.prio)
            return false;
    }
    return true;
}

inline constexpr std::array kFifoOrder{
    SeatTag::RT,
    SeatTag::Frame,
    SeatTag::Input,
    SeatTag::Io,
    SeatTag::Pcm,
    SeatTag::Capture,
    SeatTag::Prefetch
};

inline constexpr std::array kRtCpuSeats{SeatTag::RT, SeatTag::Frame};

constexpr bool fifo_order_holds(
    const ThreadMap& m,
    std::span<const SeatTag> order = kFifoOrder
) noexcept {
    std::size_t fifo_rows = 0;
    for (const ThreadRole& r : m) {
        if (r.policy == SchedPolicy::Fifo)
            ++fifo_rows;
    }
    if (fifo_rows != order.size())
        return false;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const ThreadRole& r = seat_of(m, order[i]);
        if (r.policy != SchedPolicy::Fifo)
            return false;
        if (i > 0 && r.prio >= seat_of(m, order[i - 1]).prio)
            return false;
    }
    return true;
}

constexpr bool rt_cpu_reserved(
    const ThreadMap& m,
    std::span<const SeatTag> allowed = kRtCpuSeats
) noexcept {
    const int rt = seat_of(m, SeatTag::RT).cpu;
    for (const ThreadRole& r : m) {
        if (r.cpu != rt)
            continue;
        bool listed = false;
        for (const SeatTag t : allowed)
            listed = listed || t == r.seat;
        if (!listed)
            return false;
    }
    return true;
}

inline constexpr std::array kDdrMappingOwners{SeatTag::Pcm, SeatTag::Ui, SeatTag::Io};

constexpr bool ddr_owners_are(
    const ThreadMap& m,
    std::span<const SeatTag> owners = kDdrMappingOwners
) noexcept {
    for (const ThreadRole& r : m) {
        bool listed = false;
        for (const SeatTag t : owners)
            listed = listed || t == r.seat;
        if ((r.ddr == DdrMapping::Owner) != listed)
            return false;
    }
    return true;
}

constexpr bool policy_prio_consistent(const ThreadMap& m) noexcept {
    for (const ThreadRole& r : m) {
        if (r.policy == SchedPolicy::Fifo) {
            if (r.prio < 1 || r.prio > 99)
                return false;
        } else if (r.prio != 0) {
            return false;
        }
    }
    return true;
}

constexpr bool spawn_stack_consistent(const ThreadMap& m) noexcept {
    for (const ThreadRole& r : m) {
        if (r.spawn == SpawnKind::Create && r.stack_bytes == 0)
            return false;
        if (r.spawn == SpawnKind::PromoteInPlace && r.stack_bytes != 0)
            return false;
    }
    return true;
}

constexpr SeatTag highest_fifo_seat(const ThreadMap& m) noexcept {
    const ThreadRole* best = nullptr;
    for (const ThreadRole& r : m) {
        if (r.policy != SchedPolicy::Fifo)
            continue;
        if (best == nullptr || r.prio > best->prio)
            best = &r;
    }
    return best != nullptr ? best->seat : m[0].seat;
}

constexpr bool names_fit_comm(const std::array<SeatNameRow, kSeatTagCount>& rows) noexcept {
    for (const SeatNameRow& r : rows) {
        if (r.second == nullptr || name_len(r.second) == 0)
            return false;
        if (name_len(r.second) > kCommRoleMax)
            return false;
    }
    return true;
}

constexpr int max_cpu(const ThreadMap& m) noexcept {
    int hi = 0;
    for (const ThreadRole& r : m) {
        if (r.cpu > hi)
            hi = r.cpu;
    }
    return hi;
}

constexpr bool cpus_within(const ThreadMap& m, long online_cpus) noexcept {
    if (online_cpus <= 0)
        return false;
    for (const ThreadRole& r : m) {
        if (r.cpu < 0 || static_cast<long>(r.cpu) >= online_cpus)
            return false;
    }
    return true;
}

constexpr bool main_off_rt_cpu(const ThreadMap& m, int main_cpu) noexcept {
    if (main_cpu == seat_of(m, SeatTag::RT).cpu)
        return false;
    for (const ThreadRole& r : m) {
        if (r.cpu == main_cpu)
            return true;
    }
    return false;
}

void adopt_placement(const ThreadMap& m, SeatTag seat) noexcept;

bool widen_self_affinity() noexcept;

bool restore_self_affinity() noexcept;

}
