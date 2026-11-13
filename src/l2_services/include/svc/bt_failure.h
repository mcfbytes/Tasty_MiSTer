// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::svc {

enum class BtFailure : std::uint8_t {
    None,
    AuthenticationFailed,
    AuthenticationRejected,
    AuthenticationCanceled,
    ConnectionAttemptFailed,
    PairTimedOut,
    Failed,
};

static_assert(std::is_trivially_copyable_v<BtFailure>);
static_assert(std::has_unique_object_representations_v<BtFailure>);

}
