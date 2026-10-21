// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "infra/byte_view.h"
#include "infra/seat.h"
#include "os/anon_mapping.h"

namespace mister::cores::mra {

class RomBytes {
    TASTY_SEAT_EXEMPT(component);

public:
    RomBytes() noexcept = default;
    ~RomBytes() = default;

    RomBytes(RomBytes&& o) noexcept : map_(std::move(o.map_)), size_(std::exchange(o.size_, 0)) {}

    RomBytes& operator=(RomBytes&& o) noexcept {
        map_ = std::move(o.map_);
        size_ = std::exchange(o.size_, 0);
        return *this;
    }

    RomBytes(const RomBytes&) = delete;
    RomBytes& operator=(const RomBytes&) = delete;

    [[nodiscard]] bool resize(std::size_t bytes) noexcept;

    void clear() noexcept { size_ = 0; }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] std::size_t capacity() const noexcept { return map_.size(); }

    [[nodiscard]] std::uint8_t* data() noexcept { return bytes_(); }

    [[nodiscard]] const std::uint8_t* data() const noexcept { return bytes_(); }

    [[nodiscard]] std::span<const std::uint8_t> view() const noexcept { return {bytes_(), size_}; }

    [[nodiscard]] const std::uint8_t* begin() const noexcept { return bytes_(); }

    [[nodiscard]] const std::uint8_t* end() const noexcept { return bytes_() + size_; }

    [[nodiscard]] std::uint8_t front() const noexcept { return bytes_()[0]; }

    [[nodiscard]] std::uint8_t back() const noexcept { return bytes_()[size_ - 1]; }

    std::uint8_t& operator[](std::size_t i) noexcept { return bytes_()[i]; }

    const std::uint8_t& operator[](std::size_t i) const noexcept { return bytes_()[i]; }

    [[nodiscard]] bool operator==(std::span<const std::uint8_t> other) const noexcept {
        return std::equal(begin(), end(), other.begin(), other.end());
    }

private:
    [[nodiscard]] std::uint8_t* bytes_() noexcept {
        return infra::byte_cast<std::uint8_t>(map_.view()).data();
    }

    [[nodiscard]] const std::uint8_t* bytes_() const noexcept {
        return infra::byte_cast<const std::uint8_t>(map_.view()).data();
    }

    os::AnonMapping map_;
    std::size_t size_ = 0;
};

}
