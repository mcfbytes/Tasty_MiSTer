// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::os {

enum class KernelRt : std::uint8_t { Unknown, PreemptRt, NotRt };

}
