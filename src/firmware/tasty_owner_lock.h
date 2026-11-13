// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "os/process_id.h"
#include "tasty_paths.h"

namespace mister::fw {

class TastyOwnerLock {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] static Ex<TastyOwnerLock> take(const TastyPaths& paths) noexcept;
    TastyOwnerLock(TastyOwnerLock&&) noexcept = default;
    TastyOwnerLock& operator=(TastyOwnerLock&&) noexcept = default;
    TastyOwnerLock(const TastyOwnerLock&) = delete;
    TastyOwnerLock& operator=(const TastyOwnerLock&) = delete;
    ~TastyOwnerLock() = default;

    void release() noexcept;

    [[nodiscard]] Ex<void> write_pid(os::ProcessId pid, std::string_view movie = {}) const noexcept;

private:
    TastyOwnerLock(TastyPaths paths, UniqueFd fd) noexcept;

    TastyPaths paths_{};
    UniqueFd fd_{};
};

}
