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
#include "app/link_tx_channel.h"
#include "app/load_belt.h"
#include "app/mount_status_cell.h"
#include "app/owner_tally.h"
#include "app/path_text.h"
#include "app/save_extent_cell.h"
#include "app/save_state_attach.h"
#include "app/save_state_facts.h"
#include "cores/ladder_host.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/status_cell.h"

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
        infra::OptRef<SaveStateAttach::AskInbox> save_state_asks;
        infra::OptRef<const SaveStateFactsCell> save_state_facts;
        const LoadBelt& belt;
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
        std::uint64_t offset
    ) override;
    [[nodiscard]] Ex<proto::FileId> intern_whole(
        std::vector<std::uint8_t>&& bytes,
        std::string_view path
    ) override;
    [[nodiscard]] cores::FileTxAnswer file_tx_answer(proto::ActKey act) override;
    [[nodiscard]] bool order_load_facts(std::span<const std::uint8_t> facts) override;
    [[nodiscard]] std::size_t link_in_flight() const noexcept override;
    [[nodiscard]] cores::WireWordBytes link_word_bytes() const noexcept override;
    [[nodiscard]] proto::ActKey next_generation() noexcept override;
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

private:
    const Edges edges_;
    std::optional<WalkAsk> walk_{};
};

}
