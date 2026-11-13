// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>

#include "infra/seat.h"
#include "os/process_id.h"
#include "tasty_paths.h"

namespace mister::fw {

class TastyPidFile {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit TastyPidFile(const TastyPaths& paths) noexcept : paths_(paths) {}

    [[nodiscard]] std::optional<os::ProcessId> pid() const noexcept;
    [[nodiscard]] std::optional<std::string> movie() const noexcept;

    [[nodiscard]] std::string busy_text() const noexcept;

private:
    TastyPaths paths_;
};

}
