// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string_view>

#include "cores/companion_ask.h"
#include "cores/companion_load.h"
#include "cores/companion_plan.h"
#include "cores/loader.h"
#include "cores/mailbox_servants.h"
#include "cores/msu_machine.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "svc/vfs.h"

namespace mister::cores {

class MsuCompanion final {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit MsuCompanion(const svc::Vfs& vfs) noexcept : vfs_(&vfs) {}

    MsuCompanion(const MsuCompanion&) = delete;
    MsuCompanion& operator=(const MsuCompanion&) = delete;

    [[nodiscard]] Ex<CompanionPlan> plan(const CompanionAsk& ask);
    [[nodiscard]] std::unique_ptr<ILoader> loader();

    [[nodiscard]] ServantId servant() const noexcept {
        TASTY_SEAT_BODY(MsuCompanion);
        return ServantId::Msu1;
    }

    [[nodiscard]] static std::string_view stem_of(std::string_view path) noexcept;

private:
    const svc::Vfs* vfs_;
    FixedStr<MsuMachine::kPathCap, StrFit::Reject> stem_{};
    FixedStr<MsuMachine::kPathCap, StrFit::Reject> data_{};
};

}
