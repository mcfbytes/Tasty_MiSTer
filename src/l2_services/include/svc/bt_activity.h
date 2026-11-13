// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>

#include "infra/seat.h"
#include "os/bt_addr.h"

namespace mister::svc {

class BtActivity {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class Kind : std::uint8_t { Idle, Discovering, Pairing, Connecting };

    constexpr BtActivity() noexcept = default;

    [[nodiscard]] static constexpr BtActivity idle() noexcept { return BtActivity{}; }

    [[nodiscard]] static constexpr BtActivity discovering() noexcept {
        return BtActivity{Kind::Discovering, os::BtAddr{}};
    }

    [[nodiscard]] static constexpr BtActivity pairing(os::BtAddr addr) noexcept {
        return BtActivity{Kind::Pairing, addr};
    }

    [[nodiscard]] static constexpr BtActivity connecting(os::BtAddr addr) noexcept {
        return BtActivity{Kind::Connecting, addr};
    }

    [[nodiscard]] constexpr Kind kind() const noexcept { return kind_; }

    [[nodiscard]] constexpr bool busy() const noexcept {
        return kind_ == Kind::Pairing || kind_ == Kind::Connecting;
    }

    [[nodiscard]] constexpr std::optional<os::BtAddr> device() const noexcept {
        if (!busy())
            return std::nullopt;
        return addr_;
    }

    friend constexpr bool operator==(const BtActivity&, const BtActivity&) = default;

private:
    constexpr BtActivity(Kind kind, os::BtAddr addr) noexcept : kind_(kind), addr_(addr) {}

    Kind kind_ = Kind::Idle;
    os::BtAddr addr_{};
};

static_assert(std::is_trivially_copyable_v<BtActivity>);
static_assert(std::has_unique_object_representations_v<BtActivity>);
static_assert(sizeof(BtActivity) == sizeof(std::uint8_t) + os::BtAddr::kOctets);

}
