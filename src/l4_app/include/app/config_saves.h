// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/board_ops.h"
#include "app/config_slot_cell.h"
#include "app/ini_cell.h"
#include "app/event.h"
#include "app/link_rx_channel.h"
#include "app/link_tx_channel.h"
#include "app/negotiated_core.h"
#include "app/owner_tally.h"
#include "infra/counter.h"
#include "cores/config_codec.h"
#include "cores/types.h"
#include "infra/error.h"
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

enum class SlotLoad : std::uint8_t { NoRole, Refused, Bound };

class ConfigSaves {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Wiring {
        LinkTxChannel<SeatTag::Main>& inbox;
        const LinkRxChannel<SeatTag::Main>& rx;
        OwnerTally& tally;
        EventQueue<SeatTag::Main>& owner_events;
        const svc::Vfs& vfs;
        const proto::StatusCell& status_cell;
        const NegotiatedCore& negotiated;
        infra::OptRef<ISleeper> sleeper;
        const IniCell& ini;
    };

    explicit ConfigSaves(const Wiring& w) noexcept;
    ConfigSaves(const ConfigSaves&) = delete;
    ConfigSaves& operator=(const ConfigSaves&) = delete;

    void save_config(CorrelationTag tag);
    void save_dips(CorrelationTag tag) noexcept;
    void save_slot(cores::ConfigSlot slot, CorrelationTag tag) noexcept;

    [[nodiscard]] SlotLoad load_slot(cores::ConfigSlot slot, CorrelationTag tag);
    void set_ini_key(std::string_view key, std::string_view value);

    void on_save_bytes(const proto::LinkEvent::SaveBytes& e);

    void on_slot_value(const proto::LinkEvent::SaveBytes& e);

    void publish_slot_previews();

    [[nodiscard]] std::string slot_rel(cores::ConfigSlot slot) const;

    void order_save_upload_() noexcept;
    [[nodiscard]] bool order_save_ask(
        proto::SaveKind kind,
        cores::ConfigSlot slot,
        CorrelationTag tag,
        bool gating = false
    ) noexcept;

    [[nodiscard]] const ConfigSlotCell& config_slot_cell() const noexcept {
        return config_slot_cell_;
    }

    [[nodiscard]] const xthread::Counter& save_write_failures_cell() const noexcept {
        return tally_.save_write_failures;
    }

    [[nodiscard]] const xthread::Counter& ini_write_failures_cell() const noexcept {
        return tally_.ini_write_failures;
    }

private:
    void publish_save_verdict_(bool ok, CorrelationTag tag) noexcept;
    [[nodiscard]] bool write_durably_(std::string_view rel, std::span<const std::byte> bytes);

    [[nodiscard]] std::vector<std::uint8_t> read_slot_(std::string_view rel) const;
    [[nodiscard]] bool bind_slot_value(std::string_view rel);

    [[nodiscard]] Ex<proto::FileId> intern_value_(std::span<const std::byte> value);
    [[nodiscard]] infra::OptRef<const cores::IConfigCodec> codec_() const noexcept;

    LinkTxChannel<SeatTag::Main>& inbox_;
    const LinkRxChannel<SeatTag::Main>& rx_;
    OwnerTally& tally_;
    EventQueue<SeatTag::Main>::Producer owner_events_;
    const svc::Vfs& vfs_;
    const proto::StatusCell& status_cell_;
    const NegotiatedCore& negotiated_;
    infra::OptRef<ISleeper> sleeper_;
    const IniCell& ini_;
    cores::ConfigSlot slot_save_slot_{};
    ConfigSlotTable preview_scratch_{};
    ConfigSlotCell config_slot_cell_{};
};

}
