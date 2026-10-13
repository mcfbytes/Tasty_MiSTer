// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <linux/input.h>

#include <cstddef>
#include <optional>
#include <span>

#include "infra/seat.h"
#include "infra/unique_fd.h"

namespace mister::os {

class Evdev {
    TASTY_SEAT_EXEMPT(component);

public:
    Evdev() = default;

    explicit Evdev(int fd) noexcept : fd_(fd) {}
    Evdev(Evdev&&) noexcept = default;
    Evdev& operator=(Evdev&&) noexcept = default;
    Evdev(const Evdev&) = delete;
    Evdev& operator=(const Evdev&) = delete;

    [[nodiscard]] bool valid() const noexcept { return fd_.valid(); }
    [[nodiscard]] int fd() const noexcept { return fd_.get(); }

    [[nodiscard]] ssize_t read(std::span<input_event> out) const noexcept;

    [[nodiscard]] std::optional<input_absinfo> abs_info(unsigned code) const noexcept;

    [[nodiscard]] std::optional<input_id> id() const noexcept;

    [[nodiscard]] std::optional<std::size_t> name(std::span<char> out) const noexcept;
    [[nodiscard]] std::optional<std::size_t> uniq(std::span<char> out) const noexcept;

    [[nodiscard]] std::optional<int> effect_count() const noexcept;

    [[nodiscard]] bool has_ff(unsigned code) const noexcept;

    [[nodiscard]] bool grab(bool on) const noexcept;

    [[nodiscard]] bool erase_effect(int id) const noexcept;

    [[nodiscard]] bool upload_effect(ff_effect& e) const noexcept;

private:
    UniqueFd fd_{};
};

}  // namespace mister::os
