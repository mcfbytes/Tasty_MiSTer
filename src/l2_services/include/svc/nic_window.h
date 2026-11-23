// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "infra/byte_offset.h"
#include "svc/nic_slot_word.h"

namespace mister::svc {

class INicWindow {
public:
    virtual ~INicWindow() = default;
    [[nodiscard]] virtual std::byte load(infra::ByteOffset at) const noexcept = 0;
    virtual void store(infra::ByteOffset at, std::byte b) noexcept = 0;

    [[nodiscard]] virtual NicSlotWord load_slot(infra::ByteOffset at) const noexcept = 0;
    virtual void store_slot(infra::ByteOffset at, NicSlotWord w) noexcept = 0;

protected:
    INicWindow() = default;
    INicWindow(const INicWindow&) = default;
    INicWindow& operator=(const INicWindow&) = default;
};

}
