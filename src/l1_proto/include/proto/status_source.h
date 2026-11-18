// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "proto/status_change.h"
#include "proto/status_word.h"

namespace mister::proto {

class IStatusSource {
public:
    virtual ~IStatusSource() = default;

    [[nodiscard]] virtual std::optional<StatusChange> take(
        StatusChange held,
        StatusWord& words
    ) noexcept = 0;

protected:
    IStatusSource() = default;
    IStatusSource(const IStatusSource&) = default;
    IStatusSource& operator=(const IStatusSource&) = default;
};

}
