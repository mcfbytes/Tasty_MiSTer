// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/session_params.h"
#include "proto/types.h"

namespace mister::proto {

class IImageSink {
public:
    virtual ~IImageSink() = default;

    [[nodiscard]] virtual Ex<void> begin(WideIoIndex index, const SessionParams& params) = 0;
    [[nodiscard]] virtual Ex<void> write(std::span<const std::uint8_t> data) = 0;
    [[nodiscard]] virtual Ex<void> end() = 0;

protected:
    IImageSink() = default;
    IImageSink(const IImageSink&) = default;
    IImageSink& operator=(const IImageSink&) = default;
};

}  // namespace mister::proto
