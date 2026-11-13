// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "infra/error.h"
#include "os/bt_addr.h"

namespace mister::os {

class BtDevicePath {
public:
    static constexpr std::size_t kMaxAdapterDigits = 3;
    static constexpr std::uint32_t kMaxAdapter = 255;
    static constexpr std::size_t kMaxChars = 40;

    [[nodiscard]] static Ex<BtDevicePath> parse(std::string_view path) noexcept;

    [[nodiscard]] std::uint8_t adapter() const noexcept { return adapter_; }

    [[nodiscard]] BtAddr addr() const noexcept { return addr_; }

    class Text {
    public:
        [[nodiscard]] std::string_view view() const noexcept { return {text_.data(), length_}; }

        [[nodiscard]] const char* c_str() const noexcept { return text_.data(); }

    private:
        friend class BtDevicePath;
        std::array<char, kMaxChars + 1> text_{};
        std::size_t length_ = 0;
    };

    [[nodiscard]] Text format() const noexcept;

    friend constexpr bool operator==(const BtDevicePath&, const BtDevicePath&) = default;

private:
    std::uint8_t adapter_ = 0;
    BtAddr addr_{};
};

static_assert(std::is_trivially_copyable_v<BtDevicePath>);
static_assert(std::has_unique_object_representations_v<BtDevicePath>);
static_assert(sizeof(BtDevicePath) == 1 + BtAddr::kOctets);

}
