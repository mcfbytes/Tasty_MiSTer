// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/error.h"
#include "infra/unique_fd.h"

namespace mister::ui {

class FbTerminalHost {
public:
    [[nodiscard]] Ex<void> launch(std::string_view command_line);
    [[nodiscard]] Ex<bool> reap();

private:
    UniqueFd tty_;
};

}
