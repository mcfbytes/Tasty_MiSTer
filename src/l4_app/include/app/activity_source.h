// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

class IActivitySource {
public:
    virtual ~IActivitySource() = default;

    virtual std::uint32_t activity_seq() const noexcept = 0;

    virtual bool input_grabbed() const noexcept = 0;

protected:
    IActivitySource() = default;
    IActivitySource(const IActivitySource&) = default;
    IActivitySource& operator=(const IActivitySource&) = default;
};

}  // namespace mister::app
