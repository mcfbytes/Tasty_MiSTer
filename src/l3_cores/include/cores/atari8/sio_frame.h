// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>

#include "cores/atari8/sio_command.h"
#include "cores/atari8/sio_types.h"

namespace mister::cores::atari8 {

struct SioFrame {
    SioDevice device{};
    std::byte code{};

    SioSector sector{};

    [[nodiscard]] constexpr std::optional<SioCommand> command() const noexcept {
        return classify(code);
    }
};

}
