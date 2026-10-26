// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

namespace mister::app {

struct HdStatusSlots {
    bool bluetooth = false;
    bool rt = false;
    friend bool operator==(const HdStatusSlots&, const HdStatusSlots&) = default;
};

static_assert(std::is_trivially_copyable_v<HdStatusSlots>);
static_assert(std::is_nothrow_default_constructible_v<HdStatusSlots>);

}
