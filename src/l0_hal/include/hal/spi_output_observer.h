// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::hal {

class ISpiOutputObserver {
public:
    virtual ~ISpiOutputObserver() = default;

    virtual void on_store(std::uint32_t output) noexcept = 0;

protected:
    ISpiOutputObserver() = default;
    ISpiOutputObserver(const ISpiOutputObserver&) = default;
    ISpiOutputObserver& operator=(const ISpiOutputObserver&) = default;
};

}  // namespace mister::hal
