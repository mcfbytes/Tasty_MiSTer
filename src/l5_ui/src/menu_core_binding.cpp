// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menu_core_binding.h"

#include "app/identity_latch.h"

namespace mister::ui {

void MenuCoreBinding::set_core_type(proto::CoreType t) noexcept {
    core_type_ = t;
    ui_capable_ = (t == proto::CoreType::EightBit || t == proto::CoreType::SharpMz);
}

void MenuCoreBinding::latch_home(const app::IdentityLatch& identity) noexcept {
    app::SessionIdentity id{};
    if (identity.copy(id)) {
        if (!browse_home_.assign(app::home_name(id)))
            browse_home_.clear();
        file_stem_ = id.stem;
        pause_rule_ = id.pause;
        return;
    }
    browse_home_.clear();
    file_stem_ = app::RememberedStem{};
    pause_rule_ = cores::PauseOnOsd::Option;
}

std::string_view MenuCoreBinding::browse_home(std::string_view core_name) const noexcept {
    return browse_home_.empty() ? core_name : browse_home_.view();
}

app::RememberedStem MenuCoreBinding::file_stem(std::string_view core_name) const noexcept {
    return file_stem_.empty() ? app::RememberedStem::of(core_name, {}) : file_stem_;
}

}
