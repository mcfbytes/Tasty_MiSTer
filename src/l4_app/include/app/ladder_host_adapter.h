// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "app/act_keys.h"
#include "app/file_tx_level.h"
#include "app/link_tx_channel.h"
#include "app/load_belt.h"
#include "app/mount_status_cell.h"
#include "app/owner_tally.h"
#include "app/save_extent_cell.h"
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
    class Acts {
    public:
        [[nodiscard]] virtual bool order_walk(
            proto::IoIndex slot,
            std::string_view path,
            std::uint32_t gen
        ) = 0;
        [[nodiscard]] virtual bool order_savestates(std::string_view image, std::uint32_t gen) = 0;

    protected:
        Acts() = default;
        Acts(const Acts&) = default;
        Acts& operator=(const Acts&) = default;
        virtual ~Acts() = default;
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
        infra::OptRef<const SaveStateFactsCell> save_state_facts;
        const LoadBelt& belt;
    };

    LadderHostAdapter(const Edges& edges, Acts& acts) noexcept : edges_(edges), acts_(acts) {}

    LadderHostAdapter(const LadderHostAdapter&) = delete;
    LadderHostAdapter& operator=(const LadderHostAdapter&) = delete;

    [[nodiscard]] bool order(const proto::LinkOp& op) override;
    using cores::ILadderHost::order;
    [[nodiscard]] Ex<proto::FileId> intern_bytes(
        std::span<const std::uint8_t> bytes,
        std::string_view ext,
        std::uint32_t load_addr
    ) override;
    [[nodiscard]] Ex<proto::FileId> intern_path(
        std::string_view path,
        std::uint64_t size_bytes
    ) override;
    [[nodiscard]] Ex<proto::FileId> intern_piece(
        std::span<const std::uint8_t> bytes,
        std::string_view ext,
        std::uint64_t whole,
        std::uint64_t offset
    ) override;
    [[nodiscard]] Ex<proto::FileId> intern_whole(
        std::vector<std::uint8_t>&& bytes,
        std::string_view path
    ) override;
    [[nodiscard]] std::optional<bool> file_tx_answer(std::uint32_t act) override;
    [[nodiscard]] bool order_load_facts(std::span<const std::uint8_t> facts) override;
    [[nodiscard]] std::size_t link_in_flight() const noexcept override;
    [[nodiscard]] std::size_t link_word_bytes() const noexcept override;
    [[nodiscard]] std::uint32_t next_generation() noexcept override;
    [[nodiscard]] cores::MountStatus mount_status() override;
    [[nodiscard]] bool order_walk(
        proto::IoIndex slot,
        std::string_view path,
        std::uint32_t gen
    ) override;
    [[nodiscard]] cores::MountStatus walk_status() override;
    [[nodiscard]] bool order_savestates(std::string_view image, std::uint32_t gen) override;
    [[nodiscard]] std::uint32_t savestates_answered() override;
    [[nodiscard]] cores::SaveExtent save_extent() override;
    [[nodiscard]] proto::StatusWord status_word() override;

private:
    const Edges edges_;
    Acts& acts_;
};

}
