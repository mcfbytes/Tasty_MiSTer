// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "infra/seat.h"
#include "infra/unique_fd.h"

namespace mister::os {

class Framebuffer {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class Wait : std::uint8_t { Edge, Interrupted, Failed };

    Framebuffer() noexcept = default;

    [[nodiscard]] static Framebuffer adopt(UniqueFd fd) noexcept {
        return Framebuffer{std::move(fd)};
    }

    [[nodiscard]] bool valid() const noexcept { return fd_.valid(); }

    [[nodiscard]] int fd() const noexcept { return fd_.get(); }

    [[nodiscard]] Wait wait_vsync() const noexcept;

private:
    explicit Framebuffer(UniqueFd fd) noexcept : fd_(std::move(fd)) {}

    UniqueFd fd_{};
};

}
