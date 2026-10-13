// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/unique_fd.h"

namespace mister::os {

class Framebuffer {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class Wait : std::uint8_t { Edge, Interrupted, Failed };

    Framebuffer() noexcept = default;
    explicit Framebuffer(int fd) noexcept : fd_(fd) {}

    [[nodiscard]] bool valid() const noexcept { return fd_.valid(); }
    [[nodiscard]] int fd() const noexcept { return fd_.get(); }

    [[nodiscard]] Wait wait_vsync() const noexcept;

private:
    UniqueFd fd_{};
};

}  // namespace mister::os
