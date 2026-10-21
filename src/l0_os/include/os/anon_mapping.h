// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <utility>

#include "infra/seat.h"

namespace mister::os {

class AnonMapping {
    TASTY_SEAT_EXEMPT(component);

public:
    AnonMapping() noexcept = default;

    ~AnonMapping() { release(); }

    AnonMapping(const AnonMapping&) = delete;
    AnonMapping& operator=(const AnonMapping&) = delete;

    AnonMapping(AnonMapping&& o) noexcept : map_(std::exchange(o.map_, {})) {}

    AnonMapping& operator=(AnonMapping&& o) noexcept {
        if (this != &o) {
            release();
            map_ = std::exchange(o.map_, {});
        }
        return *this;
    }

    [[nodiscard]] bool resize(std::size_t bytes) noexcept;
    void release() noexcept;

    [[nodiscard]] std::span<std::byte> view() const noexcept { return map_; }

    [[nodiscard]] std::byte* data() noexcept { return map_.data(); }

    [[nodiscard]] const std::byte* data() const noexcept { return map_.data(); }

    [[nodiscard]] std::size_t size() const noexcept { return map_.size(); }

    [[nodiscard]] bool empty() const noexcept { return map_.empty(); }

private:
    std::span<std::byte> map_{};
};

}
