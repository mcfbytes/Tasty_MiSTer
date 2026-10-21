// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/script_policy.h"

namespace mister::ui {

class ScriptFilePolicy final : public IScriptPolicy {
public:
    OpenGate gate_open(infra::OptRef<IBtOps> bt) const override;
    bool is_pairing() const noexcept override;
    [[nodiscard]] Ex<Spawn> resolve(std::string_view argv0) const override;
    void before_spawn(bool bt_reset, infra::OptRef<IBtOps> bt) const override;
    void stop(std::string_view title) const override;
    std::string_view running_footer() const noexcept override;
    bool renews_on_back() const noexcept override;
    Transition exit_transition() const override;
};

}
