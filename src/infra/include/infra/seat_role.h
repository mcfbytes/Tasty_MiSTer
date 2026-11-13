// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>

#include "infra/seat.h"

namespace mister {

struct SeatRoleRow {
    SeatTag tag;
    const char* role;
};

inline constexpr std::array<SeatRoleRow, kSeatTagCount> kSeatRoles{{
    {SeatTag::Unbound, "unbound"},
    {SeatTag::RT, "link"},
    {SeatTag::Frame, "frame"},
    {SeatTag::Input, "input"},
    {SeatTag::Pcm, "pcm"},
    {SeatTag::Prefetch, "prefetch"},
    {SeatTag::Diag, "diag"},
    {SeatTag::Ui, "ui"},
    {SeatTag::Io, "io"},
    {SeatTag::Capture, "capture"},
    {SeatTag::Encode, "encode"},
    {SeatTag::RecWrite, "recwrite"},
    {SeatTag::Launcher, "launcher"},
    {SeatTag::HdOsd, "hdosd"},
    {SeatTag::Connect, "connect"},
    {SeatTag::Main, "main"},
}};

static_assert(tags_are_ordinal(kSeatRoles), "kSeatRoles: row i names tag i, one row per tag");

constexpr const char* seat_role(SeatTag t) noexcept {
    const auto i = static_cast<std::size_t>(t);
    return i < kSeatTagCount ? kSeatRoles[i].role : "?";
}

}
