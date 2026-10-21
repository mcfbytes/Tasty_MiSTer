// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/types.h"
#include "svc/track.h"

namespace mister::svc {

class IDiscBacking {
public:
    [[nodiscard]] Ex<void> read_payload(
        proto::Lba lba,
        const Track& t,
        std::uint32_t s_off,
        std::span<std::byte> out
    ) {
        return do_read_payload(lba, t, s_off, out);
    }

protected:
    IDiscBacking() = default;
    IDiscBacking(const IDiscBacking&) = default;
    IDiscBacking(IDiscBacking&&) = default;
    IDiscBacking& operator=(const IDiscBacking&) = default;
    IDiscBacking& operator=(IDiscBacking&&) = default;
    ~IDiscBacking() = default;

private:
    [[nodiscard]] virtual Ex<void> do_read_payload(
        proto::Lba lba,
        const Track& t,
        std::uint32_t s_off,
        std::span<std::byte> out
    ) = 0;
};

}
