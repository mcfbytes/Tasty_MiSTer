// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app/cmd_verb_sink.h"

namespace mister::app {

inline constexpr std::size_t kCmdLineMax = 1024;

enum class CmdLineOutcome : std::uint8_t { Routed, Unrouted, Unrecognised };

[[nodiscard]] CmdLineOutcome deliver_cmd_line(std::string_view line, ICmdVerbSink& sink) noexcept;

}
