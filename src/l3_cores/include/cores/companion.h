// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/companion_ask.h"
#include "cores/companion_plan.h"
#include "cores/servant_id.h"
#include "infra/error.h"

namespace mister::cores {

class ILoader;

class ICompanion {
public:
    virtual ~ICompanion() = default;

    [[nodiscard]] virtual Ex<CompanionPlan> plan(const CompanionAsk& ask) = 0;
    [[nodiscard]] virtual std::unique_ptr<ILoader> loader() = 0;
    [[nodiscard]] virtual ServantId servant() const noexcept = 0;

protected:
    ICompanion() = default;
    ICompanion(const ICompanion&) = default;
    ICompanion& operator=(const ICompanion&) = default;
};

}
