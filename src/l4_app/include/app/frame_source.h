// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "infra/error.h"

namespace mister::app {

class IFrameSource {
public:
    virtual ~IFrameSource() = default;

    struct Frame {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> rgb;
    };

    struct Counts {
        std::uint32_t captures = 0;
        std::uint32_t refused = 0;
        std::uint32_t torn = 0;
    };

    [[nodiscard]] virtual Ex<Frame> capture(bool scaled) = 0;

    [[nodiscard]] virtual Counts counts() const noexcept = 0;

protected:
    IFrameSource() = default;
    IFrameSource(const IFrameSource&) = default;
    IFrameSource& operator=(const IFrameSource&) = default;
};

}  // namespace mister::app
