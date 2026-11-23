// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "proto/user_io/poll_frame.h"

namespace mister::proto {

struct MailboxResult {

    enum class End : std::uint8_t { Done, GuardHeld, Refused, Faulted, kCount };

    End end = End::Done;

    user_io::PollFrame replies{};
};

static_assert(std::is_trivially_copyable_v<MailboxResult>);

}
