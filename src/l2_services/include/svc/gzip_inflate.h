// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::svc {

class GzipInflater final {
    TASTY_SEAT_RESIDENT(Main);

public:
    GzipInflater() noexcept;
    ~GzipInflater();
    GzipInflater(const GzipInflater&) = delete;
    GzipInflater& operator=(const GzipInflater&) = delete;

    [[nodiscard]] static bool is_gzip(std::span<const std::uint8_t> src) noexcept;

    [[nodiscard]] Ex<std::vector<std::uint8_t>> inflate(
        std::span<const std::uint8_t> src,
        proto::FileSize cap
    );

private:
    class Stream;
    std::unique_ptr<Stream> stream_;
};

}
