// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::svc {

enum class WifiFailure : std::uint8_t {
    None,
    SupplicantWontStart,
    ConfUnreadable,
    WrongKey,
    NotFound,
    Rejected,
};

static_assert(std::is_trivially_copyable_v<WifiFailure>);
static_assert(std::has_unique_object_representations_v<WifiFailure>);

}
