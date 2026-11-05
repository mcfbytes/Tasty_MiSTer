// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/write_verdict.h"
#include "infra/opt_ref.h"

namespace mister::app {

enum class VerdictSink : std::uint8_t { None, Played, Recents };

struct VerdictSinks {
    infra::OptRef<IWriteVerdict> played;
    infra::OptRef<IWriteVerdict> recents;
};

}
