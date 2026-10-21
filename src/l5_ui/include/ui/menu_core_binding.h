// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "app/launcher_state.h"
#include "app/session_identity.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/core_session.h"

namespace mister::app {
class IdentityLatch;
}

namespace mister::ui {

class MenuCoreBinding final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    void set_core_type(proto::CoreType t) noexcept;

    proto::CoreType core_type() const noexcept { return core_type_; }

    bool ui_capable() const noexcept { return ui_capable_; }

    void set_front_end(bool on) noexcept { front_end_ = on; }

    bool front_end() const noexcept { return front_end_; }

    void latch_home(const app::IdentityLatch& identity) noexcept;

    [[nodiscard]] std::string_view browse_home(std::string_view core_name) const noexcept;
    [[nodiscard]] app::RememberedStem file_stem(std::string_view core_name) const noexcept;

    cores::PauseOnOsd pause_rule() const noexcept { return pause_rule_; }

private:
    proto::CoreType core_type_ = proto::CoreType::Unknown;
    bool ui_capable_ = false;
    bool front_end_ = false;
    cores::PauseOnOsd pause_rule_ = cores::PauseOnOsd::Option;
    FixedStr<app::SessionIdentity::kMax, StrFit::Clip> browse_home_{};
    app::RememberedStem file_stem_{};
};

}
