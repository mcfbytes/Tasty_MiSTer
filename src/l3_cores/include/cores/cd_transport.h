// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/core_profile.h"
#include "infra/error.h"

namespace mister::cores {

class ICdTransport {
public:
    virtual ~ICdTransport() = default;

    virtual Ex<void> send_data(IoIndex io_index, std::span<const std::byte> payload) = 0;

    virtual Ex<void> send_status(std::uint64_t bcd_frame) = 0;

protected:
    ICdTransport() = default;
    ICdTransport(const ICdTransport&) = default;
    ICdTransport& operator=(const ICdTransport&) = default;
};

}  // namespace mister::cores
