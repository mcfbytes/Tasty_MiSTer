// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

namespace mister::fw::tasty {

class Exit {
public:
    enum class Code : std::uint8_t {
        Ok = 0,
        Refused = 1,
        BusyOwner = 2,
        Mismatch = 3,
        Wedged = 75,
        SignalBase = 128,
    };

    constexpr Exit() noexcept = default;

    constexpr Exit(Code code) noexcept : code_(code) {}

    [[nodiscard]] static constexpr Exit signal(int sig) noexcept {
        return Exit{static_cast<Code>(std::to_underlying(Code::SignalBase) + sig)};
    }

    [[nodiscard]] static constexpr Exit of_status(int status) noexcept {
        return Exit{static_cast<Code>(status)};
    }

    [[nodiscard]] constexpr int status() const noexcept { return std::to_underlying(code_); }

    [[nodiscard]] constexpr bool ok() const noexcept { return code_ == Code::Ok; }

    friend constexpr bool operator==(Exit, Exit) noexcept = default;

private:
    Code code_ = Code::Ok;
};

}
