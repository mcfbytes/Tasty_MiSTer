// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "ui/options_page.h"

namespace mister::ui {

class VersionItem final : public OptionItem {
public:
    VersionItem() noexcept : OptionItem(Kind::Version) {}

private:
    Carry on_build(const Source& in, const BuildEnv& env) override;
    std::string on_compose(const Look& look) const override;
    app::PageDescription::Row on_describe(const Look& look) const override;
    Transition on_activate(const Press& press, IOptionHost& host) const override;
};

}
