// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace mister::ui {

class ScriptRequest {
public:
    [[nodiscard]] static ScriptRequest script(std::string argv0) noexcept {
        return ScriptRequest{static_cast<std::string&&>(argv0)};
    }

    [[nodiscard]] std::string_view argv0() const noexcept { return argv0_; }

private:
    explicit ScriptRequest(std::string argv0) noexcept
        : argv0_(static_cast<std::string&&>(argv0)) {}

    std::string argv0_;
};

}
