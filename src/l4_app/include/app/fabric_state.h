// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct FabricState {
    std::uint32_t gen = 0;
    std::uint16_t cookie = 0;
    bool configured = false;
    bool bridges_down = false;
    bool load_ok = false;
};
static_assert(std::is_trivially_copyable_v<FabricState>);
static_assert(std::is_nothrow_default_constructible_v<FabricState>);

using FabricCell = xthread::Telemetry<FabricState>;

inline constexpr std::int64_t kFabricStaleNs = 5'000'000'000;

class FabricStateView {
    TASTY_SEAT_RESIDENT(RT);

public:
    void refresh(const FabricCell& cell, std::int64_t now_ns) noexcept;

    [[nodiscard]] bool configured() const noexcept { return last_.configured && !stale_; }
    [[nodiscard]] bool stale() const noexcept { return stale_; }

    [[nodiscard]] bool unconfigured() const noexcept {
        return last_.bridges_down && !last_.load_ok;
    }
    [[nodiscard]] std::uint16_t cookie() const noexcept { return last_.cookie; }
    [[nodiscard]] std::uint32_t gen() const noexcept { return last_.gen; }
    [[nodiscard]] const FabricState& value() const noexcept { return last_; }

    [[nodiscard]] std::uint32_t expiries() const noexcept { return expiries_; }

    [[nodiscard]] bool ever_fresh() const noexcept { return has_sample_; }

private:
    FabricCell::Reader reader_{};
    FabricState last_{};
    std::int64_t fresh_ns_ = 0;
    std::uint32_t expiries_ = 0;
    bool has_sample_ = false;
    bool stale_ = false;
};

}  // namespace mister::app
