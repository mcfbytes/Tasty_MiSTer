// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/types.h"

namespace mister::os {

class UioHandle;

class UioLineRef {
    TASTY_SEAT_EXEMPT(component);

public:
    UioLineRef() = default;

    int fd() const noexcept { return fd_; }

    UioLine line() const noexcept { return line_; }

    [[nodiscard]] Ex<std::uint32_t> consume() const;
    [[nodiscard]] Ex<void> rearm() const;

private:
    friend class UioHandle;

    UioLineRef(int fd, UioLine line) noexcept : fd_(fd), line_(line) {}

    int fd_ = -1;
    UioLine line_{};
};

}
