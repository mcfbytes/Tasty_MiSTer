// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/companion_ask.h"
#include "cores/companion_plan.h"
#include "cores/loader.h"
#include "cores/mailbox_servants.h"
#include "infra/error.h"
#include "svc/vfs.h"

namespace mister::cores {

class ICompanionLoad {
public:
    virtual ~ICompanionLoad() = default;

    [[nodiscard]] virtual Ex<CompanionPlan> plan(const CompanionAsk& ask) = 0;

    [[nodiscard]] virtual std::unique_ptr<ILoader> loader() = 0;

    [[nodiscard]] virtual ServantId servant() const noexcept = 0;

protected:
    ICompanionLoad() = default;
    ICompanionLoad(const ICompanionLoad&) = default;
    ICompanionLoad& operator=(const ICompanionLoad&) = default;
};

using MakeCompanion = std::unique_ptr<ICompanionLoad> (*)(const svc::Vfs&);

}  // namespace mister::cores
