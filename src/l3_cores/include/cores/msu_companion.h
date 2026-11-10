// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/companion.h"
#include "cores/companion_ask.h"
#include "cores/companion_plan.h"
#include "cores/loader.h"
#include "cores/servant_id.h"
#include "infra/path_text.h"
#include "infra/seat.h"
#include "svc/vfs.h"

namespace mister::cores {

class MsuCompanion final : public ICompanion {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit MsuCompanion(const svc::Vfs& vfs) noexcept : vfs_(vfs) {}

    MsuCompanion(const MsuCompanion&) = delete;
    MsuCompanion& operator=(const MsuCompanion&) = delete;

    [[nodiscard]] Ex<CompanionPlan> plan(const CompanionAsk& ask) override;
    [[nodiscard]] std::unique_ptr<ILoader> loader() override;

    [[nodiscard]] ServantId servant() const noexcept override {
        TASTY_SEAT_BODY(MsuCompanion);
        return ServantId::Msu1;
    }

private:
    const svc::Vfs& vfs_;

    infra::PathText stem_{};
    infra::PathText data_{};
};

}
