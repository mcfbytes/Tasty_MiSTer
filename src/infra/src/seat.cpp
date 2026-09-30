// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/seat.h"

#include <cstdio>

#include "infra/persist.h"

namespace mister {
namespace {

thread_local SeatTag g_seat TASTY_PERSIST(proc, seat_current) = SeatTag::Unbound;

}

SeatTag current_seat() noexcept { return g_seat; }

SeatTag adopt_seat_tag(SeatTag t) noexcept {
    const SeatTag prev = g_seat;
    g_seat = t;
    return prev;
}

namespace seat_detail {

[[noreturn]] void violation(SeatTag want, SeatTag got, std::uint16_t site, const char* what) {
    const auto detail = static_cast<std::uint32_t>((static_cast<std::uint32_t>(want) << 8) |
                                                   static_cast<std::uint32_t>(got));
    std::fprintf(stderr, "SEAT VIOLATION: %s — declared %s, running on %s\n", what, seat_name(want),
                 seat_name(got));
    fatal(Error{Errc::negotiation, site, detail}, what);
}

}  // namespace seat_detail
}  // namespace mister
