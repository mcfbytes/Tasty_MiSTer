// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::svc {

enum class PromptAnswer : std::uint8_t {
    Accept,
    Reject,
    Cancel,
};

static_assert(std::is_trivially_copyable_v<PromptAnswer>);
static_assert(std::has_unique_object_representations_v<PromptAnswer>);

}
