// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace mister::cores::x86 {

struct DosPath {
    static constexpr std::string_view kRoot = ".";

    DosPath() = default;

    [[nodiscard]] static DosPath of(std::string_view guest_name);

    [[nodiscard]] std::string_view view() const noexcept { return text_; }

    [[nodiscard]] bool is_root() const noexcept { return text_ == kRoot; }

    [[nodiscard]] std::string_view parent() const noexcept;

    [[nodiscard]] std::string_view leaf() const noexcept;

    [[nodiscard]] std::string under(std::string_view name) const;

    friend bool operator==(const DosPath&, const DosPath&) = default;

private:
    explicit DosPath(std::string text) noexcept : text_(std::move(text)) {}

    std::string text_{kRoot};
};

}
