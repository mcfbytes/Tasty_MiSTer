// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "hal/ocram_nonce.h"
#include "hal/ocram_ring_desc.h"
#include "hal/ocram_types.h"
#include "infra/byte_offset.h"

namespace mister::hal::ocram {

class Window;
template <class Owner>
class RingWriter;
template <class Owner>
class RingReader;

class RingLend {
public:
    RingLend() = default;

    [[nodiscard]] static RingLend over(
        std::span<std::uint32_t> words,
        RingDesc desc,
        Nonce nonce
    ) noexcept {
        if (words.empty() || !desc.fits_in(infra::ByteOffset{words.size_bytes()})) {
            return RingLend{};
        }
        return RingLend(reinterpret_cast<volatile std::byte*>(words.data()), desc, nonce);
    }

    [[nodiscard]] bool lent() const noexcept { return base_ != nullptr; }

    [[nodiscard]] RingDesc desc() const noexcept { return desc_; }

    [[nodiscard]] Nonce nonce() const noexcept { return nonce_; }

private:
    friend class Window;
    template <class Owner>
    friend class RingWriter;
    template <class Owner>
    friend class RingReader;

    constexpr RingLend(volatile std::byte* base, RingDesc desc, Nonce nonce) noexcept
        : base_(base),
          desc_(desc),
          nonce_(nonce) {}

    volatile std::byte* base_ = nullptr;
    RingDesc desc_{};
    Nonce nonce_{};
};

static_assert(std::is_trivially_copyable_v<RingLend>);

}
