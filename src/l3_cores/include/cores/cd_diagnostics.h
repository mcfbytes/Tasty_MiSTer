// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "svc/disc_counters.h"

namespace mister::cores {

struct CdFlow;

class ICdDiagnostics {
public:
    virtual ~ICdDiagnostics() = default;

    [[nodiscard]] virtual std::optional<svc::DiscCounters> disc_counters() const noexcept = 0;
    [[nodiscard]] virtual std::optional<CdFlow> cd_flow() const noexcept = 0;

protected:
    ICdDiagnostics() = default;
    ICdDiagnostics(const ICdDiagnostics&) = default;
    ICdDiagnostics& operator=(const ICdDiagnostics&) = default;
};

}  // namespace mister::cores
