// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <tuple>

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
}  // namespace mister::app
namespace mister::svc {
class PrefetchMain;
class IoMain;
}  // namespace mister::svc
namespace mister::reactor {
class FrameMain;
}

namespace mister::fw {

class DiagMain;
class UiMain;

template <SeatTag S, class M>
struct SeatRow {
    static constexpr SeatTag seat = S;
    using Main = M;
};

template <class... Rows>
struct SeatList {};

using SeatMainList =
    SeatList<SeatRow<SeatTag::RT, app::RtMain>, SeatRow<SeatTag::Frame, reactor::FrameMain>,
             SeatRow<SeatTag::Input, app::InputMain>, SeatRow<SeatTag::Pcm, app::PcmMain>,
             SeatRow<SeatTag::Prefetch, svc::PrefetchMain>, SeatRow<SeatTag::Diag, DiagMain>,
             SeatRow<SeatTag::Ui, UiMain>, SeatRow<SeatTag::Io, svc::IoMain>,
             SeatRow<SeatTag::Capture, app::CaptureMain>, SeatRow<SeatTag::Encode, app::EncodeMain>,
             SeatRow<SeatTag::RecWrite, app::RecWriteMain>,
             SeatRow<SeatTag::Launcher, app::LauncherMain>,
             SeatRow<SeatTag::HdOsd, app::HdOsdMain>>;

constexpr std::size_t seat_index(SeatTag s) noexcept { return hal::row_index(s); }

template <class... Rows>
consteval bool rows_seat_each_once(SeatList<Rows...>) {
    std::array<int, hal::kThreadSeats> seen{};
    ((++seen[seat_index(Rows::seat)]), ...);
    for (const int n : seen) {
        if (n != 1) return false;
    }
    return sizeof...(Rows) == hal::kThreadSeats;
}
static_assert(rows_seat_each_once(SeatMainList{}),
              "every seat has exactly one main row: a missing row leaves a seat unspawnable");

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

struct DrainEdge {
    SeatTag seat;
    SeatTag producer;
};

inline constexpr std::array kSpawnOrder{
    SeatTag::Diag,     SeatTag::Ui,    SeatTag::Prefetch, SeatTag::Pcm,    SeatTag::Frame,
    SeatTag::Input,    SeatTag::Io,    SeatTag::RecWrite, SeatTag::Encode, SeatTag::Capture,
    SeatTag::Launcher, SeatTag::HdOsd, SeatTag::RT,
};
inline constexpr std::array kStopOrder{
    SeatTag::RT,      SeatTag::Diag,     SeatTag::Ui,    SeatTag::Frame,
    SeatTag::Input,   SeatTag::Prefetch, SeatTag::Pcm,   SeatTag::Io,
    SeatTag::Capture, SeatTag::Launcher, SeatTag::HdOsd,
};
inline constexpr std::array kStopAfterJoinOf{
    DrainEdge{SeatTag::Encode, SeatTag::Capture},
    DrainEdge{SeatTag::RecWrite, SeatTag::Encode},
};
inline constexpr std::array kJoinOrder{
    SeatTag::RT,       SeatTag::Io,    SeatTag::Capture, SeatTag::Encode,   SeatTag::RecWrite,
    SeatTag::Launcher, SeatTag::HdOsd, SeatTag::Pcm,     SeatTag::Prefetch, SeatTag::Frame,
    SeatTag::Input,    SeatTag::Ui,    SeatTag::Diag,
};

template <std::size_t N>
consteval std::size_t position(const std::array<SeatTag, N>& order, SeatTag s) {
    for (std::size_t i = 0; i < N; ++i) {
        if (order[i] == s) return i;
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
        if (n != 1) return false;
    }
    return N == hal::kThreadSeats;
}

template <std::size_t D>
constexpr bool drains_after_join(const std::array<DrainEdge, D>& drains, SeatTag s) noexcept {
    for (const DrainEdge& e : drains) {
        if (e.seat == s) return true;
    }
    return false;
}

template <std::size_t N, std::size_t D>
consteval bool spawn_order_holds(const std::array<SeatTag, N>& spawn,
                                 const std::array<DrainEdge, D>& drains) {
    if (!covers_each_seat_once(spawn) || spawn[N - 1] != SeatTag::RT) return false;
    for (const DrainEdge& e : drains) {
        if (!before(spawn, e.seat, e.producer)) return false;
    }
    return true;
}

template <std::size_t N, std::size_t D>
consteval bool stop_order_holds(const std::array<SeatTag, N>& stop,
                                const std::array<DrainEdge, D>& drains) {
    if (N == 0 || stop[0] != SeatTag::RT) return false;
    std::array<int, hal::kThreadSeats> seen{};
    for (const SeatTag s : stop)
        ++seen[seat_index(s)];
    for (const DrainEdge& e : drains)
        ++seen[seat_index(e.seat)];
    for (const int n : seen) {
        if (n != 1) return false;
    }
    return true;
}

template <std::size_t N, std::size_t D>
consteval bool join_order_holds(const std::array<SeatTag, N>& join,
                                const std::array<DrainEdge, D>& drains) {
    if (!covers_each_seat_once(join)) return false;
    if (join[0] != SeatTag::RT || join[1] != SeatTag::Io) return false;
    if (join[N - 1] != SeatTag::Diag) return false;
    if (!before(join, SeatTag::Input, SeatTag::Ui)) return false;
    for (const DrainEdge& e : drains) {
        if (!before(join, e.producer, e.seat)) return false;
    }
    return true;
}

static_assert(spawn_order_holds(kSpawnOrder, kStopAfterJoinOf));
static_assert(stop_order_holds(kStopOrder, kStopAfterJoinOf));
static_assert(join_order_holds(kJoinOrder, kStopAfterJoinOf));

}  // namespace mister::fw
