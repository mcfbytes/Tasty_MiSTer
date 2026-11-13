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

class DbusObjectPath {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kMaxLength = 255;
    static constexpr std::size_t kStore = kMaxLength + 1;

    constexpr DbusObjectPath() noexcept = default;

    [[nodiscard]] static Ex<DbusObjectPath> parse(std::string_view path) noexcept;

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return length_.text_of(std::span<const char>{text_});
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return length_ == WireU16{0}; }

    friend constexpr bool operator==(const DbusObjectPath&, const DbusObjectPath&) = default;

private:
    explicit DbusObjectPath(std::string_view path) noexcept;

    std::array<char, kStore> text_{};
    WireU16 length_{};
};

static_assert(std::is_trivially_copyable_v<DbusObjectPath>);
static_assert(std::has_unique_object_representations_v<DbusObjectPath>);
static_assert(sizeof(DbusObjectPath) == DbusObjectPath::kStore + sizeof(WireU16));

}
