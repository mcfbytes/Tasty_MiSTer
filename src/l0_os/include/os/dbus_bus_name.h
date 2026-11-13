// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/types.h"

namespace mister::os {

class DbusBusName {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kMaxLength = 255;
    static constexpr std::size_t kStore = kMaxLength + 1;

    constexpr DbusBusName() noexcept = default;

    [[nodiscard]] static Ex<DbusBusName> parse(std::string_view name) noexcept;

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return length_.text_of(std::span<const char>{text_});
    }

    [[nodiscard]] constexpr bool unique() const noexcept {
        return length_ != WireU16{0} && text_[0] == ':';
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return length_ == WireU16{0}; }

    friend constexpr bool operator==(const DbusBusName&, const DbusBusName&) = default;

private:
    explicit DbusBusName(std::string_view name) noexcept;

    std::array<char, kStore> text_{};
    WireU16 length_{};
};

static_assert(std::is_trivially_copyable_v<DbusBusName>);
static_assert(std::has_unique_object_representations_v<DbusBusName>);
static_assert(sizeof(DbusBusName) == DbusBusName::kStore + sizeof(WireU16));

}
