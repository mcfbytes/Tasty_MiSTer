// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>

#include "ui/option_item.h"

namespace mister::ui {

class VersionItem final : public OptionItem {
public:
    VersionItem() noexcept : OptionItem(Kind::Version) {}

private:
    Carry on_build(const Source& in, const BuildEnv& env) override;
    std::string on_compose(const Look& look) const override;
    std::optional<app::PageDescription::Row> on_describe(const Look& look) const override;
    Transition on_select(IOptionHost& host) const override;
    Transition on_step(CycleDir dir, proto::FieldValue value, IOptionHost& host) const override;
};

}
