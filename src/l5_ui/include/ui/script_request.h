// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "ui/script_kind.h"

namespace mister::ui {

class ScriptRequest {
public:
    [[nodiscard]] static ScriptRequest script(std::string argv0) noexcept {
        return ScriptRequest{ScriptKind::Script, static_cast<std::string&&>(argv0), false};
    }

    [[nodiscard]] static ScriptRequest bt_pair(bool reset_adapter) noexcept {
        return ScriptRequest{ScriptKind::BtPair, std::string{}, reset_adapter};
    }

    [[nodiscard]] ScriptKind kind() const noexcept { return kind_; }

    [[nodiscard]] std::string_view argv0() const noexcept { return argv0_; }

    [[nodiscard]] bool reset_adapter() const noexcept { return reset_adapter_; }

private:
    ScriptRequest(ScriptKind kind, std::string argv0, bool reset_adapter) noexcept
        : kind_(kind),
          argv0_(static_cast<std::string&&>(argv0)),
          reset_adapter_(reset_adapter) {}

    ScriptKind kind_;
    std::string argv0_;
    bool reset_adapter_;
};

}
