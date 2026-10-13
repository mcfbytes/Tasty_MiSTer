// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace mister::os {

void touch_stack_pages(std::size_t span, std::size_t page_bytes) noexcept;

}
