// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/cheat_catalog_cell.h"
#include "app/cheat_store.h"
#include "app/conf_str_cell.h"
#include "app/config_slot_cell.h"
#include "app/dip_cell.h"
#include "app/option_cell.h"
#include "app/tx_digest_cell.h"
#include "infra/seat.h"
#include "proto/conf_str.h"
#include "proto/core_session.h"
#include "proto/status_cell.h"
#include "ui/status_view.h"

namespace mister::app {
class IdentityLatch;
}

namespace mister::ui {

class CoreCellMirror final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Wiring {
        const app::ConfStrCell& conf_str;
        const proto::StatusCell& status;
        const app::DipCell& dip;
        const app::OptionCell& option;
        const app::ConfigSlotCell& config_slot;
        const app::TxDigestCell& tx_digest;
        const app::CheatCatalogCell& cheat_catalog;
        app::CheatStore& cheats;
        const app::IdentityLatch& identity;
    };

    explicit CoreCellMirror(const Wiring& w) noexcept
        : conf_cell_(w.conf_str),
          status_cell_(w.status),
          dip_cell_(w.dip),
          option_cell_(w.option),
          config_slot_cell_(w.config_slot),
          tx_digest_cell_(w.tx_digest),
          cheat_catalog_cell_(w.cheat_catalog),
          cheats_(w.cheats),
          identity_(w.identity) {}

    struct Changed {
        std::optional<proto::ItemTable> table{};
        bool repaint = false;
    };

    [[nodiscard]] Changed sync();

    [[nodiscard]] bool pending() const noexcept;

    void forget() noexcept;

    [[nodiscard]] bool set_status_words(StatusView s) noexcept;

    const StatusView& status_words() const noexcept { return status_words_; }

    const app::DipTable& dip_table() const noexcept { return dip_scratch_; }

    const app::OptionTable& option_table() const noexcept { return option_scratch_; }

    const app::ConfigSlotTable& config_slot_table() const noexcept { return config_slot_scratch_; }

    proto::BindGeneration conf_gen() const noexcept { return conf_gen_; }

    std::uint32_t cell_refusals() const noexcept {
        return conf_cell_.refusals() + status_cell_.refusals();
    }

    std::uint32_t conf_reparses() const noexcept { return conf_reparses_; }

    std::uint32_t conf_failures() const noexcept { return conf_failures_; }

    std::uint32_t conf_truncations() const noexcept { return conf_truncations_; }

    std::uint32_t status_reads() const noexcept { return status_reads_; }

    std::uint32_t cheat_opens() const noexcept { return cheat_opens_; }

    std::uint32_t cheat_open_failures() const noexcept { return cheat_open_failures_; }

private:
    [[nodiscard]] bool sync_cheats_();

    const app::ConfStrCell& conf_cell_;
    const proto::StatusCell& status_cell_;
    const app::DipCell& dip_cell_;
    const app::OptionCell& option_cell_;
    const app::ConfigSlotCell& config_slot_cell_;
    const app::TxDigestCell& tx_digest_cell_;
    const app::CheatCatalogCell& cheat_catalog_cell_;
    app::CheatStore& cheats_;
    const app::IdentityLatch& identity_;
    StatusView status_words_{};
    app::ConfStrCell::Reader conf_reader_{};
    proto::StatusCell::Reader status_reader_{};
    app::DipCell::Reader dip_reader_{};
    app::DipTable dip_scratch_{};
    app::OptionCell::Reader option_reader_{};
    app::OptionTable option_scratch_{};
    app::ConfigSlotCell::Reader config_slot_reader_{};
    app::ConfigSlotTable config_slot_scratch_{};
    app::TxDigestCell::Reader tx_digest_reader_{};
    app::CheatCatalogCell::Reader cheat_catalog_reader_{};
    app::TxDigest tx_digest_scratch_{};
    app::CheatCatalog cheat_catalog_scratch_{};
    app::ConfStrText conf_scratch_{};
    proto::BindGeneration conf_gen_{};
    proto::StatusWord status_scratch_{};
    std::uint32_t cheat_opens_ = 0;
    std::uint32_t cheat_open_failures_ = 0;
    std::uint32_t conf_reparses_ = 0;
    std::uint32_t conf_failures_ = 0;
    std::uint32_t conf_truncations_ = 0;
    std::uint32_t status_reads_ = 0;
};

}
