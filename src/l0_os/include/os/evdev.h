// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <linux/input.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "infra/byte_view.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "os/ff_effect_id.h"

namespace mister::os {

class Evdev {
    TASTY_SEAT_EXEMPT(component);

public:
    Evdev() = default;

    [[nodiscard]] static Evdev adopt(UniqueFd fd) noexcept { return Evdev{std::move(fd)}; }

    Evdev(Evdev&&) noexcept = default;
    Evdev& operator=(Evdev&&) noexcept = default;
    Evdev(const Evdev&) = delete;
    Evdev& operator=(const Evdev&) = delete;

    [[nodiscard]] bool valid() const noexcept { return fd_.valid(); }

    [[nodiscard]] int fd() const noexcept { return fd_.get(); }

    [[nodiscard]] Ex<std::size_t> read(std::span<input_event> out) const noexcept;

    [[nodiscard]] std::optional<input_absinfo> abs_info(unsigned code) const noexcept;

    [[nodiscard]] std::optional<input_id> id() const noexcept;

    [[nodiscard]] std::optional<std::string_view> name(std::span<char> out) const noexcept;
    [[nodiscard]] std::optional<std::string_view> uniq(std::span<char> out) const noexcept;

    [[nodiscard]] std::optional<int> effect_count() const noexcept;

    [[nodiscard]] bool has_ff(unsigned code) const noexcept;

    [[nodiscard]] Ex<void> grab(bool on) const noexcept;

    [[nodiscard]] Ex<void> erase_effect(FfEffectId id) const noexcept;

    [[nodiscard]] Ex<FfEffectId> upload_effect(const ff_effect& e) const noexcept;

    [[nodiscard]] Ex<void> play(FfEffectId id) const noexcept;

private:
    explicit Evdev(UniqueFd fd) noexcept : fd_(std::move(fd)) {}

    UniqueFd fd_{};
};

}
