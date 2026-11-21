// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/cd/cd_sector_act.h"
#include "cores/cd/cdwire.h"
#include "cores/core_profile.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"

namespace mister::cores::cd {

class ICdLink {
public:
    virtual ~ICdLink() = default;

    [[nodiscard]] virtual Ex<void> send_data(
        IoIndex io_index,
        std::span<const std::byte> payload
    ) = 0;

    [[nodiscard]] virtual Ex<void> send_status(std::span<const hal::SpiWord> words) = 0;

    [[nodiscard]] virtual bool can_send_data(cdwire::Selector) { return true; }

    [[nodiscard]] virtual bool egress_bound() const noexcept { return false; }

    [[nodiscard]] virtual bool egress_idle() const noexcept { return true; }

    [[nodiscard]] virtual Ex<void> submit_sector(const CdSectorAct&) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }

protected:
    ICdLink() = default;
    ICdLink(const ICdLink&) = default;
    ICdLink& operator=(const ICdLink&) = default;
};

}
