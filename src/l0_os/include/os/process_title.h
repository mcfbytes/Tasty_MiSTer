// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::os {

class ProcessTitle {
    TASTY_SEAT_RESIDENT(Ui);

public:
    [[nodiscard]] static Ex<ProcessTitle> create(int argc, char** argv, char**& env) noexcept;

    ProcessTitle(ProcessTitle&&) noexcept = default;
    ProcessTitle& operator=(ProcessTitle&&) = delete;
    ProcessTitle(const ProcessTitle&) = delete;
    ProcessTitle& operator=(const ProcessTitle&) = delete;

    void set(std::string_view title) noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept { return region_.size(); }
    [[nodiscard]] std::uint32_t truncations() const noexcept { return truncations_; }

private:
    explicit ProcessTitle(std::span<char> region) noexcept : region_(region) {}

    std::span<char> region_{};
    std::uint32_t truncations_ = 0;
};

}  // namespace mister::os
