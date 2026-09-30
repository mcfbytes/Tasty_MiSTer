// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::infra {

struct ClaimSetCounts {
    std::uint32_t claimed = 0;
    std::uint32_t refused = 0;
    std::uint32_t returned = 0;
    std::uint32_t high_water = 0;
};

template <class Id, std::size_t N>
class ClaimSet {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(std::is_enum_v<Id>, "a claim is named by an enumerator");
    static_assert(N > 0 && N <= 64, "the held set is one 64-bit word");

public:
    class Claim {
        TASTY_SEAT_EXEMPT(component);

    public:
        Claim() noexcept = default;
        Claim(Claim&& o) noexcept : set_(std::exchange(o.set_, nullptr)), id_(o.id_) {}
        Claim& operator=(Claim&& o) noexcept {
            if (this != &o) {
                reset();
                set_ = std::exchange(o.set_, nullptr);
                id_ = o.id_;
            }
            return *this;
        }
        Claim(const Claim&) = delete;
        Claim& operator=(const Claim&) = delete;
        ~Claim() { reset(); }

        [[nodiscard]] explicit operator bool() const noexcept { return set_ != nullptr; }
        [[nodiscard]] Id id() const noexcept { return id_; }

        void reset() noexcept {
            if (set_ != nullptr) std::exchange(set_, nullptr)->give_back_(id_);
        }

    private:
        friend class ClaimSet;
        Claim(ClaimSet* set, Id id) noexcept : set_(set), id_(id) {}

        ClaimSet* set_ = nullptr;
        Id id_{};
    };

    ClaimSet() noexcept = default;

    ClaimSet(ClaimSet&&) = delete;
    ClaimSet& operator=(ClaimSet&&) = delete;
    ClaimSet(const ClaimSet&) = delete;
    ClaimSet& operator=(const ClaimSet&) = delete;

    ~ClaimSet() { assert(held_ == 0 && "ClaimSet destroyed with a claim out"); }

    [[nodiscard]] Ex<Claim> claim(Id id) noexcept {
        const auto i = static_cast<std::size_t>(id);
        if (i >= N) {
            ++counts_.refused;
            return std::unexpected(
                Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(i)});
        }
        const std::uint64_t bit = std::uint64_t{1} << i;
        if ((held_ & bit) != 0u) {
            ++counts_.refused;
            return std::unexpected(Error{Errc::busy, ERR_SITE(), static_cast<std::uint32_t>(i)});
        }
        held_ |= bit;
        ++held_n_;
        ++counts_.claimed;
        if (held_n_ > counts_.high_water) counts_.high_water = held_n_;
        return Claim{this, id};
    }

    [[nodiscard]] bool held(Id id) const noexcept {
        const auto i = static_cast<std::size_t>(id);
        return i < N && (held_ & (std::uint64_t{1} << i)) != 0u;
    }
    [[nodiscard]] std::size_t held_count() const noexcept { return held_n_; }
    [[nodiscard]] ClaimSetCounts counts() const noexcept { return counts_; }
    static constexpr std::size_t capacity() noexcept { return N; }

private:
    void give_back_(Id id) noexcept {
        const std::uint64_t bit = std::uint64_t{1} << static_cast<std::size_t>(id);
        if ((held_ & bit) == 0u) return;
        held_ &= ~bit;
        --held_n_;
        ++counts_.returned;
    }

    std::uint64_t held_ = 0;
    std::uint32_t held_n_ = 0;
    ClaimSetCounts counts_{};
};

}  // namespace mister::infra
