// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "os/process_id.h"

namespace mister::os {

class ProcessName {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kMax = 15;

    [[nodiscard]] static constexpr std::optional<ProcessName> of(std::string_view s) noexcept {
        if (s.empty() || s.size() > kMax)
            return std::nullopt;
        for (const char c : s) {
            if (c == '\0' || c == '\n')
                return std::nullopt;
        }
        ProcessName name;
        if (!name.text_.assign(s))
            return std::nullopt;
        return name;
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept { return text_.view(); }

    void adopt() const noexcept;

    [[nodiscard]] bool names(ProcessId pid) const noexcept;

private:
    constexpr ProcessName() noexcept = default;

    FixedStr<kMax + 1, StrFit::Reject> text_{};
};

}
