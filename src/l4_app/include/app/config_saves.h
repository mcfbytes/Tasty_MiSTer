// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "app/board_ops.h"
#include "app/event.h"
#include "app/link_rx_channel.h"
#include "app/link_tx_channel.h"
#include "app/negotiated_core.h"
#include "app/owner_tally.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/link_event.h"
#include "proto/link_op.h"
#include "proto/status_cell.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class ConfigSaves {
    TASTY_SEAT_RESIDENT(Main);

public:
    using Count = xthread::Telemetry<std::uint32_t, SeatTag::Main>;

    struct Wiring {
        LinkTxChannel<SeatTag::Main>& inbox;
        const LinkRxChannel<SeatTag::Main>& rx;
        OwnerTally& tally;
        EventQueue<SeatTag::Main>& owner_events;
        const svc::Vfs& vfs;
        const proto::StatusCell& status_cell;
        const NegotiatedCore& negotiated;
        infra::OptRef<ISleeper> sleeper;
    };

    explicit ConfigSaves(const Wiring& w) noexcept;
    ConfigSaves(const ConfigSaves&) = delete;
    ConfigSaves& operator=(const ConfigSaves&) = delete;

    void save_config(CorrelationTag tag);
    void save_dips(CorrelationTag tag) noexcept;
    void save_slot(std::uint8_t slot, CorrelationTag tag) noexcept;
    void load_slot(std::uint8_t slot, CorrelationTag tag) noexcept;
    void set_ini_key(std::string_view key, std::string_view value);

    void on_save_bytes(const proto::LinkEvent::SaveBytes& e);

    [[nodiscard]] bool order_save_ask(
        proto::SaveKind kind,
        std::uint8_t slot,
        CorrelationTag tag,
        bool gating = false
    ) noexcept;
    void order_slot_previews(const std::byte* pack, std::size_t len);
    [[nodiscard]] bool bind_slot_config(std::uint8_t slot, std::string_view path);

    [[nodiscard]] std::uint8_t load_slot_slot() const noexcept { return slot_load_slot_; }

    [[nodiscard]] const Count& save_write_failures_cell() const noexcept { return swf_cell_; }

    [[nodiscard]] const Count& ini_write_failures_cell() const noexcept { return iwf_cell_; }

private:
    void order_save_upload_() noexcept;
    void publish_save_verdict_(bool ok, CorrelationTag tag) noexcept;
    [[nodiscard]] bool write_durably_(std::string_view rel, std::span<const std::byte> bytes);
    [[nodiscard]] bool intern_rel_at_(proto::FileId id, std::string_view rel);

    LinkTxChannel<SeatTag::Main>& inbox_;
    const LinkRxChannel<SeatTag::Main>& rx_;
    OwnerTally& tally_;
    EventQueue<SeatTag::Main>& owner_events_;
    const svc::Vfs& vfs_;
    const proto::StatusCell& status_cell_;
    const NegotiatedCore& negotiated_;
    infra::OptRef<ISleeper> sleeper_;
    std::uint8_t slot_load_slot_ = 0;
    std::uint8_t slot_save_slot_ = 0;
    Count swf_cell_{};
    Count iwf_cell_{};
};

}
