// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::os {

class ProcessId {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class Presence : std::uint8_t { Gone, Alive, Unreachable };

    [[nodiscard]] static constexpr std::optional<ProcessId> of(int raw) noexcept {
        if (raw <= 0)
            return std::nullopt;
        return ProcessId{raw};
    }

    [[nodiscard]] static ProcessId self() noexcept;
    [[nodiscard]] bool is_self() const noexcept;
    [[nodiscard]] Presence presence() const noexcept;
    [[nodiscard]] Ex<void> terminate() const noexcept;
    [[nodiscard]] Ex<void> kill_now() const noexcept;

    [[nodiscard]] constexpr int printed() const noexcept { return v_; }

    friend constexpr bool operator==(ProcessId, ProcessId) = default;

private:
    explicit constexpr ProcessId(int v) noexcept : v_(v) {}

    int v_;
};

}
