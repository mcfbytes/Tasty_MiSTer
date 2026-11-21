// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/recents/write_verdict.h"
#include "infra/opt_ref.h"

namespace mister::app::recents {

enum class VerdictSink : std::uint8_t { None, Played, Recents, Maps };

struct VerdictSinks {
    infra::OptRef<IWriteVerdict> played;
    infra::OptRef<IWriteVerdict> recents;
    infra::OptRef<IWriteVerdict> maps{};
};

}
