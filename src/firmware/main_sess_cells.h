// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/fallback_counts.h"
#include "infra/counter.h"
#include "infra/telemetry.h"

namespace mister::fw {

struct MainSessCells {
    template <class T>
    using Main = xthread::Telemetry<T, SeatTag::Main>;

    const xthread::Counter& pause_expiries;
    const Main<std::uint32_t>& recover_polls;
    const xthread::Counter& save_write_failures;
    const Main<app::FallbackCounts>& fallbacks;
    const xthread::Counter& ini_write_failures;
};

}
