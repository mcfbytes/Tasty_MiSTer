// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/link_halves.h"
#include "hal/types.h"
#include "proto/disk_address.h"
#include "proto/mailbox_words.h"

namespace mister::cores::x86 {

struct BusRegisterWrite {
    proto::DiskAddress at{};
    hal::LinkWord word{};

    [[nodiscard]] constexpr proto::MailboxWords mailbox_words() const noexcept {
        const hal::LinkHalves a = at.halves();
        return proto::MailboxWords{a.low, a.high, word};
    }

    friend constexpr bool operator==(const BusRegisterWrite&, const BusRegisterWrite&) = default;
};

}
