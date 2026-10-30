// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "app/board_ops.h"
#include "app/link_tx_channel.h"
#include "app/mra_facts.h"
#include "app/owner_tally.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/conf_str.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

[[nodiscard]] std::array<std::string_view, 4> bind_identity_fields(
    const proto::ConfStr& conf
) noexcept;

class BindTail {
    TASTY_SEAT_RESIDENT(Main);

public:
    BindTail(
        LinkTxChannel<SeatTag::Main>& inbox,
        OwnerTally& tally,
        const svc::Vfs& vfs,
        infra::OptRef<ISleeper> sleeper
    ) noexcept
        : inbox_(inbox),
          tally_(tally),
          vfs_(vfs),
          sleeper_(sleeper) {}

    [[nodiscard]] bool identity(const proto::ConfStr& conf);
    void doorbells(
        const proto::ConfStr& conf,
        std::span<const proto::AxiLwDecl> lw,
        std::optional<std::uint32_t> owned
    );
    void joy(std::string_view core_name) noexcept;
    void facts(const MraFacts& facts) noexcept;
    void uart(std::string_view core_name, const proto::ConfStr* conf);
    [[nodiscard]] bool reset_level(bool asserted);

private:
    LinkTxChannel<SeatTag::Main>& inbox_;
    OwnerTally& tally_;
    const svc::Vfs& vfs_;
    infra::OptRef<ISleeper> sleeper_;
};

}
