// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/types.h>

#include <type_traits>

namespace mister::os {

class GroupId {
public:
    constexpr GroupId() noexcept = default;

    [[nodiscard]] static constexpr GroupId of(gid_t gid) noexcept { return GroupId{gid}; }

    [[nodiscard]] constexpr gid_t value() const noexcept { return gid_; }

    friend constexpr bool operator==(const GroupId&, const GroupId&) = default;

private:
    constexpr explicit GroupId(gid_t gid) noexcept : gid_(gid) {}

    gid_t gid_ = 0;
};

static_assert(std::is_trivially_copyable_v<GroupId>);
static_assert(std::has_unique_object_representations_v<GroupId>);
static_assert(sizeof(GroupId) == sizeof(gid_t));

}
