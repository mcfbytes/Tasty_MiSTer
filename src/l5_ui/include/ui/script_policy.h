// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "ui/script_kind.h"
#include "ui/transition.h"

namespace mister::ui {

class IBtOps;

class IScriptPolicy {
public:
    struct Spawn {
        std::string cmd;
        std::string title;
    };

    virtual ~IScriptPolicy() = default;

    enum class OpenGate : std::uint8_t { Proceed, Refuse, NoAdapter };
    virtual OpenGate gate_open(infra::OptRef<IBtOps> bt) const = 0;

    virtual bool is_pairing() const noexcept = 0;

    [[nodiscard]] virtual Ex<Spawn> resolve(std::string_view argv0) const = 0;

    virtual void before_spawn(bool bt_reset, infra::OptRef<IBtOps> bt) const = 0;
    virtual void stop(std::string_view title) const = 0;
    virtual std::string_view running_footer() const noexcept = 0;
    virtual bool renews_on_back() const noexcept = 0;
    virtual Transition exit_transition() const = 0;

protected:
    IScriptPolicy() = default;
    IScriptPolicy(const IScriptPolicy&) = default;
};

const IScriptPolicy& script_policy_for(ScriptKind kind) noexcept;

}
