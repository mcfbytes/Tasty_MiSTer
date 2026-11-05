// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>

#include "seat_row.h"
#include "hal/thread_map.h"

namespace mister::app {
class RtMain;
class InputMain;
class PcmMain;
class CaptureMain;
class EncodeMain;
class RecWriteMain;
class LauncherMain;
class HdOsdMain;
}

namespace mister::svc {
class PrefetchMain;
class IoMain;
}

namespace mister::reactor {
class FrameMain;
}

namespace mister::fw {

class DiagMain;
class UiMain;

template <class... Rows>
struct SeatList {};

using SeatMainList = SeatList<
    SeatRow<SeatTag::RT, app::RtMain>,
    SeatRow<SeatTag::Frame, reactor::FrameMain, Presence::ConfigDependent>,
    SeatRow<SeatTag::Input, app::InputMain>,
    SeatRow<SeatTag::Pcm, app::PcmMain>,
    SeatRow<SeatTag::Prefetch, svc::PrefetchMain>,
    SeatRow<SeatTag::Diag, DiagMain>,
    SeatRow<SeatTag::Ui, UiMain>,
    SeatRow<SeatTag::Io, svc::IoMain>,
    SeatRow<SeatTag::Capture, app::CaptureMain, Presence::ConfigDependent>,
    SeatRow<SeatTag::Encode, app::EncodeMain, Presence::ConfigDependent>,
    SeatRow<SeatTag::RecWrite, app::RecWriteMain, Presence::ConfigDependent>,
    SeatRow<SeatTag::Launcher, app::LauncherMain, Presence::ConfigDependent>,
    SeatRow<SeatTag::HdOsd, app::HdOsdMain, Presence::ConfigDependent>>;

constexpr std::size_t seat_index(SeatTag s) noexcept {
    return hal::row_index(s);
}

template <class... Rows>
consteval std::array<Presence, kSeatTagCount> presence_table(SeatList<Rows...>) {
    std::array<Presence, kSeatTagCount> table{};
    ((table[static_cast<std::size_t>(Rows::seat)] = Rows::presence), ...);
    return table;
}

inline constexpr auto kSeatPresence = presence_table(SeatMainList{});

constexpr Presence presence_of(SeatTag s) noexcept {
    const auto i = static_cast<std::size_t>(s);
    return i < kSeatTagCount ? kSeatPresence[i] : Presence::Required;
}

template <class... Rows>
consteval bool rows_seat_each_once(SeatList<Rows...>) {
    std::array<int, hal::kThreadSeats> seen{};
    ((++seen[seat_index(Rows::seat)]), ...);
    for (const int n : seen) {
        if (n != 1)
            return false;
    }
    return sizeof...(Rows) == hal::kThreadSeats;
}

static_assert(
    rows_seat_each_once(SeatMainList{}),
    "every seat has exactly one main row: a missing row leaves a seat unspawnable"
);

template <class List>
class SeatSlots;

template <class... Rows>
class SeatSlots<SeatList<Rows...>> {
public:
    constexpr SeatSlots() noexcept = default;

    template <class... M>
        requires(sizeof...(M) > 0)
    constexpr explicit SeatSlots(M*... mains) noexcept {
        (bind(mains), ...);
    }

    template <class M>
    constexpr void bind(M* main) noexcept {
        std::get<M*>(slots_) = main;
    }

    template <class M>
    [[nodiscard]] constexpr M* get() const noexcept {
        return std::get<M*>(slots_);
    }

    [[nodiscard]] constexpr bool bound(SeatTag s) const noexcept {
        bool hit = false;
        ((Rows::seat == s ? (hit = get<typename Rows::Main>() != nullptr) : false), ...);
        return hit;
    }

private:
    std::tuple<typename Rows::Main*...> slots_{};
};

using MainSlots = SeatSlots<SeatMainList>;

struct SeatGroup {
    std::span<const SeatTag> seats;
};

inline constexpr SeatTag kRecorderSeats[]{SeatTag::Capture, SeatTag::Encode, SeatTag::RecWrite};
inline constexpr std::array kSeatGroups{SeatGroup{.seats = kRecorderSeats}};

consteval bool every_grouped_seat_is_config_dependent() {
    for (const SeatGroup& g : kSeatGroups) {
        for (const SeatTag s : g.seats) {
            if (presence_of(s) != Presence::ConfigDependent)
                return false;
        }
    }
    return true;
}

consteval bool seats_are_grouped_once_at_most() {
    std::array<int, kSeatTagCount> seen{};
    for (const SeatGroup& g : kSeatGroups) {
        for (const SeatTag s : g.seats) {
            if (++seen[static_cast<std::size_t>(s)] > 1)
                return false;
        }
    }
    return true;
}

static_assert(
    every_grouped_seat_is_config_dependent(),
    "a group is a configuration's all-or-none, so every member is "
    "Presence::ConfigDependent; a Required seat cannot be opted out of"
);
static_assert(seats_are_grouped_once_at_most(), "a seat belongs to at most one group");

enum class SeatContract : std::uint8_t { Any, Declared, EverySeat };

template <class Bound>
constexpr SeatTag first_absent_seat(Bound&& bound, SeatContract contract) noexcept {
    if (contract == SeatContract::Any)
        return SeatTag::Unbound;
    const bool every = contract == SeatContract::EverySeat;
    for (std::size_t i = 1; i < static_cast<std::size_t>(SeatTag::Main); ++i) {
        const SeatTag s = static_cast<SeatTag>(i);
        if (!every && (s == SeatTag::RT || presence_of(s) != Presence::Required))
            continue;
        if (!bound(s))
            return s;
    }
    for (const SeatGroup& g : kSeatGroups) {
        std::size_t held = 0;
        SeatTag missing = SeatTag::Unbound;
        for (const SeatTag s : g.seats) {
            if (bound(s)) {
                ++held;
            } else if (missing == SeatTag::Unbound) {
                missing = s;
            }
        }
        if (held != 0 && held != g.seats.size())
            return missing;
    }
    return SeatTag::Unbound;
}

struct DrainEdge {
    SeatTag seat;
    SeatTag producer;
};

inline constexpr std::array kSpawnOrder{
    SeatTag::Diag,
    SeatTag::Ui,
    SeatTag::Prefetch,
    SeatTag::Pcm,
    SeatTag::Frame,
    SeatTag::Input,
    SeatTag::Io,
    SeatTag::RecWrite,
    SeatTag::Encode,
    SeatTag::Capture,
    SeatTag::Launcher,
    SeatTag::HdOsd,
    SeatTag::RT,
};
inline constexpr std::array kStopOrder{
    SeatTag::RT,
    SeatTag::Diag,
    SeatTag::Ui,
    SeatTag::Frame,
    SeatTag::Input,
    SeatTag::Prefetch,
    SeatTag::Pcm,
    SeatTag::Io,
    SeatTag::Capture,
    SeatTag::Launcher,
    SeatTag::HdOsd,
};
inline constexpr std::array kStopAfterJoinOf{
    DrainEdge{SeatTag::Encode, SeatTag::Capture},
    DrainEdge{SeatTag::RecWrite, SeatTag::Encode},
};
inline constexpr std::array kJoinOrder{
    SeatTag::RT,
    SeatTag::Io,
    SeatTag::Capture,
    SeatTag::Encode,
    SeatTag::RecWrite,
    SeatTag::Launcher,
    SeatTag::HdOsd,
    SeatTag::Pcm,
    SeatTag::Prefetch,
    SeatTag::Frame,
    SeatTag::Input,
    SeatTag::Ui,
    SeatTag::Diag,
};

template <std::size_t N>
consteval std::size_t position(const std::array<SeatTag, N>& order, SeatTag s) {
    for (std::size_t i = 0; i < N; ++i) {
        if (order[i] == s)
            return i;
    }
    return N;
}

template <std::size_t N>
consteval bool before(const std::array<SeatTag, N>& order, SeatTag a, SeatTag b) {
    return position(order, a) < position(order, b) && position(order, b) < N;
}

template <std::size_t N>
consteval bool covers_each_seat_once(const std::array<SeatTag, N>& order) {
    std::array<int, hal::kThreadSeats> seen{};
    for (const SeatTag s : order)
        ++seen[seat_index(s)];
    for (const int n : seen) {
        if (n != 1)
            return false;
    }
    return N == hal::kThreadSeats;
}

template <std::size_t D>
constexpr bool drains_after_join(const std::array<DrainEdge, D>& drains, SeatTag s) noexcept {
    for (const DrainEdge& e : drains) {
        if (e.seat == s)
            return true;
    }
    return false;
}

template <std::size_t N, std::size_t D>
consteval bool spawn_order_holds(
    const std::array<SeatTag, N>& spawn,
    const std::array<DrainEdge, D>& drains
) {
    if (!covers_each_seat_once(spawn) || spawn[N - 1] != SeatTag::RT)
        return false;
    for (const DrainEdge& e : drains) {
        if (!before(spawn, e.seat, e.producer))
            return false;
    }
    return true;
}

template <std::size_t N, std::size_t D>
consteval bool stop_order_holds(
    const std::array<SeatTag, N>& stop,
    const std::array<DrainEdge, D>& drains
) {
    if (N == 0 || stop[0] != SeatTag::RT)
        return false;
    std::array<int, hal::kThreadSeats> seen{};
    for (const SeatTag s : stop)
        ++seen[seat_index(s)];
    for (const DrainEdge& e : drains)
        ++seen[seat_index(e.seat)];
    for (const int n : seen) {
        if (n != 1)
            return false;
    }
    return true;
}

template <std::size_t N, std::size_t D>
consteval bool join_order_holds(
    const std::array<SeatTag, N>& join,
    const std::array<DrainEdge, D>& drains
) {
    if (!covers_each_seat_once(join))
        return false;
    if (join[0] != SeatTag::RT || join[1] != SeatTag::Io)
        return false;
    if (join[N - 1] != SeatTag::Diag)
        return false;
    for (const DrainEdge& e : drains) {
        if (!before(join, e.producer, e.seat))
            return false;
    }
    return true;
}

static_assert(spawn_order_holds(kSpawnOrder, kStopAfterJoinOf));
static_assert(stop_order_holds(kStopOrder, kStopAfterJoinOf));
static_assert(join_order_holds(kJoinOrder, kStopAfterJoinOf));

}
