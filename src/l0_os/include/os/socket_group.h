// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "infra/seat.h"
#include "os/group_id.h"

namespace mister::os {

class SocketGroup {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static constexpr SocketGroup none() noexcept {
        return SocketGroup{GroupId{}, kModeNone};
    }

    [[nodiscard]] static SocketGroup named(std::string_view group) noexcept;

    [[nodiscard]] static constexpr SocketGroup explicit_gid(GroupId gid) noexcept {
        return SocketGroup{gid, kModeExplicit};
    }

    [[nodiscard]] constexpr bool chowns() const noexcept { return mode_ != kModeNone; }

    [[nodiscard]] constexpr GroupId gid() const noexcept { return gid_; }

    friend constexpr bool operator==(const SocketGroup&, const SocketGroup&) = default;

private:
    static constexpr std::uint32_t kModeNone = 0;
    static constexpr std::uint32_t kModeNamed = 1;
    static constexpr std::uint32_t kModeExplicit = 2;

    constexpr SocketGroup(GroupId gid, std::uint32_t mode) noexcept : gid_(gid), mode_(mode) {}

    GroupId gid_{};
    std::uint32_t mode_ = kModeNone;
};

static_assert(
    sizeof(gid_t) == sizeof(std::uint32_t),
    "the two-word representation assumes a 32-bit gid"
);
static_assert(std::is_trivially_copyable_v<SocketGroup>);
static_assert(std::has_unique_object_representations_v<SocketGroup>);
static_assert(sizeof(SocketGroup) == sizeof(gid_t) + sizeof(std::uint32_t));

}
