// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/process_name.h"
#include "tasty_paths.h"

namespace mister::fw {

class TastyClient {
    TASTY_SEAT_RESIDENT(Main);

public:
    TastyClient(const TastyPaths& paths, os::ProcessName owner) noexcept
        : paths_(paths),
          owner_(owner) {}

    [[nodiscard]] bool owner_live() const noexcept;

    [[nodiscard]] bool joins_owner() const noexcept { return owner_live(); }

    [[nodiscard]] Ex<void> signal_owner() const noexcept;

    [[nodiscard]] Ex<void> send(std::string_view line) const noexcept;

    [[nodiscard]] Ex<std::string> status() const noexcept;

private:
    TastyPaths paths_;
    os::ProcessName owner_;
};

}
