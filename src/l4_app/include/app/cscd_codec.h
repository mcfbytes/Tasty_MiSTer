// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app/frame_arena.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::app {

class CscdCodec {
    TASTY_SEAT_EXEMPT(component);

public:
#if defined(TASTY_HAVE_LZO) && TASTY_HAVE_LZO
    static constexpr bool kAvailable = true;
#else
    static constexpr bool kAvailable = false;
#endif

    static constexpr std::size_t kHeadBytes = 2;
    static constexpr std::uint8_t kKeyBit = 0x01;

    [[nodiscard]] static constexpr std::size_t row_bytes(std::uint32_t w) noexcept {
        return (std::size_t{w} * 3u + 3u) & ~std::size_t{3};
    }
    [[nodiscard]] static constexpr std::size_t picture_bytes(std::uint32_t w,
                                                             std::uint32_t h) noexcept {
        return row_bytes(w) * h;
    }

    [[nodiscard]] static constexpr std::size_t max_payload(std::uint32_t w,
                                                           std::uint32_t h) noexcept {
        const std::size_t n = picture_bytes(w, h);
        return kHeadBytes + n + n / 16u + 64u + 3u;
    }

    CscdCodec() noexcept = default;
    CscdCodec(const CscdCodec&) = delete;
    CscdCodec& operator=(const CscdCodec&) = delete;

    [[nodiscard]] Ex<void> begin(std::uint16_t w, std::uint16_t h) noexcept;
    void end() noexcept;
    [[nodiscard]] bool open() const noexcept { return width_ != 0; }
    [[nodiscard]] std::uint16_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint16_t height() const noexcept { return height_; }
    [[nodiscard]] std::size_t mapped_bytes() const noexcept { return arena_.bytes(); }

    [[nodiscard]] std::size_t encode(const std::byte* rgb, std::size_t line, bool key,
                                     std::span<std::byte> out) noexcept;

    [[nodiscard]] std::size_t rekey(std::span<std::byte> out) noexcept;

    [[nodiscard]] std::span<const std::byte> dup() const noexcept;

    static void half_scale(const std::byte* rgb, std::size_t line, std::uint16_t w, std::uint16_t h,
                           std::byte* dst) noexcept;

private:
    [[nodiscard]] std::size_t pack_(const std::byte* src, bool key,
                                    std::span<std::byte> out) noexcept;

    FrameArena arena_{};
    std::byte* prev_ = nullptr;
    std::byte* delta_ = nullptr;
    std::byte* work_ = nullptr;
    std::byte* dup_ = nullptr;
    std::size_t dup_len_ = 0;
    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
};

}  // namespace mister::app
