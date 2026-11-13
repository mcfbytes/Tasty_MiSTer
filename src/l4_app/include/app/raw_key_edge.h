// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/inbox.h"
#include "svc/types.h"

namespace mister::app {

enum class KeyIntent : std::uint8_t { Key, IniSwitch };

struct RawKeyEdge {
    svc::EvdevCode code{};
    bool pressed = false;
    KeyIntent intent = KeyIntent::Key;
};

static_assert(sizeof(RawKeyEdge) == 4, "the ring's slot size is load-bearing");

enum MenuKeyRing : std::size_t { kMenuKeySlots = 256 };

using MenuKeyInbox = xthread::Inbox<RawKeyEdge, kMenuKeySlots>;

using KeyDownSet = svc::KeyDownSet<4>;

}
