// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/local_time.h"

namespace mister::os {

std::optional<std::tm> local_now() noexcept {
    const std::time_t now = std::time(nullptr);
    if (now == -1)
        return std::nullopt;
    std::tm local{};
    if (::localtime_r(&now, &local) == nullptr)
        return std::nullopt;
    return local;
}

}
