// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"

namespace mister::hal {
template <class Layout>
class RegisterWindow;
}

namespace mister::hal::reg {

enum class Access : std::uint8_t { RO, WO, RW };
inline constexpr Access RO = Access::RO;
inline constexpr Access WO = Access::WO;
inline constexpr Access RW = Access::RW;

inline constexpr bool readable(Access a) noexcept { return a != Access::WO; }
inline constexpr bool writable(Access a) noexcept { return a != Access::RO; }

class Block;

template <Access A>
class Reg {
    TASTY_SEAT_EXEMPT(component);

public:
    Reg() = default;

    [[nodiscard]] std::uint32_t read() const noexcept
        requires(readable(A))
    {
        return *p_;
    }
    void write(std::uint32_t v) const noexcept
        requires(writable(A))
    {
        *p_ = v;
    }
    [[nodiscard]] bool bound() const noexcept { return p_ != nullptr; }

    [[nodiscard]] static Reg over(std::uint32_t& word) noexcept { return Reg(&word); }

private:
    friend class Block;
    template <class Layout>
    friend class hal::RegisterWindow;
    explicit Reg(volatile std::uint32_t* p) noexcept : p_(p) {}

    volatile std::uint32_t* p_ = nullptr;
};

static_assert(std::is_trivially_copyable_v<Reg<WO>> && sizeof(Reg<WO>) == sizeof(void*));
static_assert(std::is_trivially_copyable_v<Reg<RO>> && sizeof(Reg<RO>) == sizeof(void*));

}  // namespace mister::hal::reg
