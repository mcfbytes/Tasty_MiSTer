// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

#include "infra/endian.h"

namespace mister::cores {

class WindowJobOrder {

    struct Room {
        alignas(std::uint64_t) std::byte b[1100]{};
    };

public:
    template <class T>
    static constexpr bool fits = sizeof(T) <= sizeof(Room);

    template <class T>
    [[nodiscard]] bool put(const T& v) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if constexpr (!fits<T>) {
            return false;
        } else {
            infra::store_native<T>(std::span<std::byte, sizeof(T)>(room_.b, sizeof(T)), v);
            len_ = sizeof(T);
            return true;
        }
    }

    template <class T>
    [[nodiscard]] std::optional<T> as() const noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if (len_ != sizeof(T))
            return std::nullopt;
        return infra::load_native<T>(std::span<const std::byte, sizeof(T)>(room_.b, sizeof(T)));
    }

private:
    Room room_;
    std::size_t len_ = 0;
};

}
