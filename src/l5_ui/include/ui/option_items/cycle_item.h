// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "ui/options_page.h"

namespace mister::ui {

class CycleItem final : public OptionItem {
public:
    CycleItem() noexcept : OptionItem(Kind::Option) {}

    [[nodiscard]] std::string_view shown_value(const Look& look) const;

private:
    Carry on_build(const Source& in, const BuildEnv& env) override;
    std::string on_compose(const Look& look) const override;
    app::PageDescription::Row on_describe(const Look& look) const override;
    Transition on_activate(const Press& press, IOptionHost& host) const override;
};

}
