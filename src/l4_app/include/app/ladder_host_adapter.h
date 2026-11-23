// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "app/act_keys.h"
#include "app/file_tx_level.h"
#include "app/game_id.h"
#include "app/ini_parse.h"
#include "app/link_tx_channel.h"
#include "app/load_belt.h"
#include "app/mount_status_cell.h"
#include "app/option_cell.h"
#include "app/config_value_cell.h"
#include "app/owner_tally.h"
#include "app/path_text.h"
#include "app/save_extent_cell.h"
#include "app/save_state_attach.h"
#include "app/save_state_facts.h"
#include "app/share_grant.h"
#include "cores/ladder_host.h"
#include "hal/fpga_aperture.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/status_cell.h"
#include "svc/nic_control.h"
#include "svc/nic_link.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class NegotiatedCore;

class LadderHostAdapter final : public cores::ILadderHost {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct WalkAsk {
        proto::IoIndex slot{};
        PathText path{};
        proto::ActKey gen{};
    };

    struct Edges {
        LinkTxChannel<SeatTag::Main>& inbox;
        OwnerTally& tally;
        ActKeys& act_keys;
        const FileTxLevelCell& ftx_level_cell;
        const NegotiatedCore& negotiated;
        const proto::StatusCell& status_cell;
        const MountStatusCell& mount_status_cell;
        const SaveExtentCell& save_extent_cell;
        const OptionCell& option_cell;
        infra::OptRef<SaveStateAttach::AskInbox> save_state_asks;
        infra::OptRef<const SaveStateFactsCell> save_state_facts;
        const LoadBelt& belt;
        const ConfigCell& config_cell;
        GameIdCell& game_id_cell;

        infra::OptRef<const ConfigValueCell> config_value_cell{};

        infra::OptRef<svc::NicControlInbox> nic{};

        infra::OptRef<ShareGrantCell> share_grant{};
        infra::OptRef<xthread::WakeFlag> io_wake{};
        infra::OptRef<const svc::Vfs> vfs{};
        hal::FpgaAperture aperture{};
    };

    explicit LadderHostAdapter(const Edges& edges) noexcept : edges_(edges) {}

    LadderHostAdapter(const LadderHostAdapter&) = delete;
    LadderHostAdapter& operator=(const LadderHostAdapter&) = delete;

    [[nodiscard]] std::optional<WalkAsk> take_walk() noexcept {
        TASTY_SEAT_BODY(LadderHostAdapter);
        return std::exchange(walk_, std::nullopt);
    }

    [[nodiscard]] bool order(const proto::LinkOp& op) override;
    using cores::ILadderHost::order;
    [[nodiscard]] Ex<proto::FileId> intern_bytes(
        std::span<const std::uint8_t> bytes,
        std::string_view ext,
        hal::FabricAddr load_addr
    ) override;
    [[nodiscard]] Ex<proto::FileId> intern_path(
        std::string_view path,
        proto::FileSize size
    ) override;
    [[nodiscard]] Ex<proto::FileId> intern_piece(
        std::span<const std::uint8_t> bytes,
        std::string_view ext,
        proto::FileSize whole,
        proto::FileOffset at
    ) override;
    [[nodiscard]] Ex<proto::FileId> intern_whole(
        std::vector<std::uint8_t>&& bytes,
        std::string_view path
    ) override;
    [[nodiscard]] cores::FileTxAnswer file_tx_answer(proto::ActKey act) override;
    [[nodiscard]] cores::FileTxAnswer payload_answer(proto::ActKey16 act) override;
    [[nodiscard]] bool order_load_facts(std::span<const std::uint8_t> facts) override;
    [[nodiscard]] std::size_t link_in_flight() const noexcept override;
    [[nodiscard]] hal::Width link_width() const noexcept override;
    [[nodiscard]] proto::ActKey next_act() noexcept override;
    [[nodiscard]] cores::MountStatus mount_status() override;
    [[nodiscard]] bool order_walk(
        proto::IoIndex slot,
        std::string_view path,
        proto::ActKey gen
    ) override;
    [[nodiscard]] cores::MountStatus walk_status() override;
    [[nodiscard]] bool order_savestates(std::string_view image, proto::ActKey gen) override;
    [[nodiscard]] proto::ActKey savestates_answered() override;
    [[nodiscard]] cores::SaveExtent save_extent() override;
    [[nodiscard]] proto::StatusWord status_word() override;
    [[nodiscard]] std::optional<proto::FieldValue> option_choice(cores::OptionTag tag) override;
    [[nodiscard]] std::span<const std::byte> config_value(std::span<std::byte> dst) override;
    [[nodiscard]] bool boot_screen() override;

    [[nodiscard]] bool start_nic(svc::NicLink link) override;

    void game_identified(std::string_view image, const svc::disc::DiscIdText& serial) override;

    void reset_share() override;

    void close_share() noexcept;

private:
    void publish_closed_() noexcept;

    const Edges edges_;
    std::optional<WalkAsk> walk_{};

    std::optional<ShareGrant::Open> share_{};
    bool open_live_ = false;
};

}
