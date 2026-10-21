// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "ui/options_page.h"

namespace mister::ui {

class ImageItem final : public OptionItem {
public:
    ImageItem() noexcept : OptionItem(Kind::Image) {}

    [[nodiscard]] bool opens_browser() const noexcept override { return true; }

    [[nodiscard]] bool mounts_image() const noexcept override { return true; }

private:
    Carry on_build(const Source& in, const BuildEnv& env) override;
    std::string on_compose(const Look& look) const override;
    app::PageDescription::Row on_describe(const Look& look) const override;
    Transition on_activate(const Press& press, IOptionHost& host) const override;
};

}
