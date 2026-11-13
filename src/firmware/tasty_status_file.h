// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "tasty_paths.h"

namespace mister::fw {

class TastyStatusFile {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit TastyStatusFile(const TastyPaths& paths) noexcept : paths_(paths) {}

    [[nodiscard]] Ex<void> write(std::string_view json) const noexcept;

private:
    TastyPaths paths_;
};

}
