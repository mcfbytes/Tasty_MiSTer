// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/config_blob.h"
#include "cores/config_codec.h"
#include "cores/types.h"
#include "infra/error.h"

namespace mister::cores {

class IConfigSlots {
public:
    virtual ~IConfigSlots() = default;

    [[nodiscard]] virtual Ex<void> apply(ConfigBlob value) = 0;

    [[nodiscard]] virtual std::span<const std::byte> value(
        std::span<std::byte> dst
    ) const noexcept = 0;

protected:
    IConfigSlots() = default;
    IConfigSlots(const IConfigSlots&) = default;
    IConfigSlots& operator=(const IConfigSlots&) = default;
};

}
