// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "infra/seat.h"
#include "os/types.h"

namespace mister::os {

class DbusAuth {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class State : std::uint8_t {
        AwaitOk = 0,
        SendBegin = 1,
        Complete = 2,
        Rejected = 3,
        kCount = 4,
    };

    enum : std::size_t { kMaxOutput = 64, kMaxLine = 64 };

    static constexpr WireBytes kBeginBytes{7};

    DbusAuth() noexcept;

    [[nodiscard]] State state() const noexcept { return state_; }

    [[nodiscard]] bool complete() const noexcept { return state_ == State::Complete; }

    [[nodiscard]] bool rejected() const noexcept { return state_ == State::Rejected; }

    [[nodiscard]] std::span<const std::byte> pending() const noexcept;
    void wrote(WireBytes bytes) noexcept;

    [[nodiscard]] WireBytes consume(std::span<const std::byte> bytes) noexcept;

private:
    [[nodiscard]] WireBytes build_auth_line() noexcept;
    void build_begin() noexcept;

    std::array<char, kMaxOutput> out_{};
    WireBytes out_length_{};
    BufferOffset out_at_{};
    std::array<char, kMaxLine> line_{};
    WireBytes line_length_{};
    State state_ = State::AwaitOk;
};

static_assert(std::is_trivially_copyable_v<DbusAuth>);
static_assert(std::is_nothrow_copy_assignable_v<DbusAuth>);

}
