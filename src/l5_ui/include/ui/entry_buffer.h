// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "infra/seat.h"

namespace mister::ui {

class EntryBuffer {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kCapacity = 63;

    EntryBuffer() noexcept = default;
    EntryBuffer(const EntryBuffer&) = delete;
    EntryBuffer& operator=(const EntryBuffer&) = delete;

    ~EntryBuffer() { scrub(); }

    [[nodiscard]] bool push(char c) noexcept;
    void pop() noexcept;
    void scrub() noexcept;

    [[nodiscard]] std::string_view view() const noexcept { return {text_.data(), length_}; }

    [[nodiscard]] std::size_t size() const noexcept { return length_; }

    [[nodiscard]] bool full() const noexcept { return length_ >= kCapacity; }

private:
    std::array<char, kCapacity> text_{};
    std::uint8_t length_ = 0;
};

}
