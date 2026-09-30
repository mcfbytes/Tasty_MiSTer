// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

class ILoadProgress {
public:
    virtual ~ILoadProgress() = default;

    virtual void on_load_progress(std::uint16_t cur, std::uint16_t max) noexcept = 0;

protected:
    ILoadProgress() = default;
    ILoadProgress(const ILoadProgress&) = default;
    ILoadProgress& operator=(const ILoadProgress&) = default;
};

}  // namespace mister::cores
