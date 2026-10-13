// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
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

    [[nodiscard]] virtual Ex<std::size_t> write_taken(std::span<const std::uint8_t> data) {
        return write(data).transform([n = data.size()]() noexcept { return n; });
    }

protected:
    IImageSink() = default;
    IImageSink(const IImageSink&) = default;
    IImageSink& operator=(const IImageSink&) = default;
};

}  // namespace mister::proto
