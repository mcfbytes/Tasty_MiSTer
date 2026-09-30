// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace mister::svc {

class IPendingWrites {
public:
    virtual ~IPendingWrites() = default;
    IPendingWrites(const IPendingWrites&) = delete;
    IPendingWrites& operator=(const IPendingWrites&) = delete;
    IPendingWrites(IPendingWrites&&) = delete;
    IPendingWrites& operator=(IPendingWrites&&) = delete;

    [[nodiscard]] virtual std::span<const std::byte> peek(std::string_view rel) const noexcept = 0;

protected:
    IPendingWrites() = default;
};

}  // namespace mister::svc
