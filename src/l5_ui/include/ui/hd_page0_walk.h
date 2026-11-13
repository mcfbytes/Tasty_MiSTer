// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "ui/build_env.h"
#include "ui/options_model.h"
#include "ui/placer.h"

namespace mister::proto {
class ItemTable;
}

namespace mister::ui {

class OptionItem;
struct CoreVerbs;

class HdPage0Walk {
public:
    TASTY_SEAT_RESIDENT(Ui);

    [[nodiscard]] static BuildEnv page0_of(const BuildEnv& env) noexcept;

    bool run(
        const proto::ItemTable& table,
        const BuildEnv& env,
        bool attempt,
        const CoreVerbs& verbs
    );

    [[nodiscard]] const OptionsModel& model() const { return model_; }

    [[nodiscard]] const Placer& placer() const { return *placer_; }

    [[nodiscard]] bool walked() const { return placer_.has_value(); }

    [[nodiscard]] infra::OptRef<const OptionItem> slot_row(proto::FileRowId want) const;

private:
    OptionsModel model_;
    std::optional<Placer> placer_;
};

}
