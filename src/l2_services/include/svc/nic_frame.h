// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/byte_view.h"
#include "infra/crc32.h"
#include "infra/endian.h"
#include "infra/seat.h"
#include "infra/span_cut.h"

namespace mister::svc {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

class NicFrame {
    TASTY_SEAT_RESIDENT(Nic);

public:
    static constexpr infra::ByteCount kPayloadMax{4096};
    static constexpr infra::ByteCount kFcsBytes{sizeof(std::uint32_t)};

    [[nodiscard]] std::span<std::byte> receive_area() noexcept {
        return infra::affine::first(all_(), kPayloadMax);
    }

    void received(infra::ByteCount n) noexcept { size_ = std::min(n, kPayloadMax); }

    [[nodiscard]] infra::ByteCount size() const noexcept { return size_; }

    [[nodiscard]] bool empty() const noexcept { return size_ == infra::ByteCount{}; }

    [[nodiscard]] std::span<std::byte> data() noexcept {
        return infra::affine::first(all_(), size_);
    }

    [[nodiscard]] std::span<const std::byte> view() const noexcept {
        return infra::affine::first(std::span<const std::byte>(bytes_), size_);
    }

    void assign(std::span<const std::byte> frame) noexcept {
        size_ = std::min(infra::ByteCount{frame.size()}, kPayloadMax);
        std::ranges::copy(infra::affine::first(frame, size_), bytes_.begin());
    }

    void clear() noexcept { size_ = infra::ByteCount{}; }

    [[nodiscard]] std::span<std::byte> room() noexcept {
        return infra::affine::first(infra::affine::from(all_(), size_), kPayloadMax - size_);
    }

    void grow(infra::ByteCount n) noexcept { size_ = std::min(size_ + n, kPayloadMax); }

    void pad_to(infra::ByteCount n) noexcept {
        const infra::ByteCount to = std::min(n, kPayloadMax);
        if (to <= size_)
            return;
        std::ranges::fill(
            infra::affine::first(infra::affine::from(all_(), size_), to - size_),
            kPad
        );
        size_ = to;
    }

    void shrink(infra::ByteCount by) noexcept { size_ -= std::min(by, size_); }

    void append_fcs() noexcept {
        infra::store_be<std::uint32_t>(
            infra::affine::from(all_(), size_).first<sizeof(std::uint32_t)>(),
            infra::crc32_of(infra::byte_cast<const std::uint8_t>(view()))
        );
        size_ += kFcsBytes;
    }

private:
    static constexpr std::byte kPad{};
    static constexpr std::size_t kCapacity = kPayloadMax.size() + kFcsBytes.size();

    [[nodiscard]] std::span<std::byte> all_() noexcept { return bytes_; }

    std::array<std::byte, kCapacity> bytes_{};
    infra::ByteCount size_{};
};

}
