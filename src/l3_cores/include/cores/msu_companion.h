// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string_view>

#include "cores/companion_load.h"
#include "cores/msu_machine.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "svc/vfs.h"

namespace mister::cores {

class MsuCompanion final : public ICompanionLoad {
    TASTY_SEAT_EXEMPT(main);

public:
    explicit MsuCompanion(const svc::Vfs& vfs) noexcept : vfs_(&vfs) {}
    MsuCompanion(const MsuCompanion&) = delete;
    MsuCompanion& operator=(const MsuCompanion&) = delete;

    [[nodiscard]] Ex<CompanionPlan> plan(const CompanionAsk& ask) override;
    [[nodiscard]] std::unique_ptr<ILoader> loader() override;
    [[nodiscard]] ServantId servant() const noexcept override { return ServantId::Msu1; }

    [[nodiscard]] static std::string_view stem_of(std::string_view path) noexcept;

private:
    const svc::Vfs* vfs_;
    FixedStr<MsuMachine::kPathCap, StrFit::Reject> stem_{};
    FixedStr<MsuMachine::kPathCap, StrFit::Reject> data_{};
};

}  // namespace mister::cores
