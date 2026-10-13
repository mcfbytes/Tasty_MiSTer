// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/user_io/poll_shape.h"

namespace mister::proto::user_io {

class IPollParams {
public:
    [[nodiscard]] virtual std::uint8_t issued_params(PollShape s) noexcept = 0;

protected:
    IPollParams() = default;
    IPollParams(const IPollParams&) = default;
    IPollParams& operator=(const IPollParams&) = default;
    ~IPollParams() = default;
};

}  // namespace mister::proto::user_io
