// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "cores/mount_status.h"
#include "cores/save_extent.h"
#include "cores/wire_word.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "proto/link_op.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::cores {

inline constexpr std::chrono::nanoseconds kSaveStateHoldBound{20'000'000'000};

enum class FileTxAnswer : std::uint8_t { Pending, Served, Dropped };

class ILadderHost {
public:
    virtual ~ILadderHost() = default;

    [[nodiscard]] virtual bool order(const proto::LinkOp& op) = 0;

    template <class A>
        requires infra::AlternativeOf<A, proto::LinkOp>
    [[nodiscard]] bool order(const A& alt) {
        return order(infra::make<proto::LinkOp>(alt));
    }

    [[nodiscard]] virtual Ex<proto::FileId> intern_bytes(
        std::span<const std::uint8_t> bytes,
        std::string_view ext,
        hal::FabricAddr load_addr
    ) = 0;

    [[nodiscard]] virtual Ex<proto::FileId> intern_piece(
        std::span<const std::uint8_t> bytes,
        std::string_view ext,
        proto::FileSize whole,
        std::uint64_t offset
    ) = 0;

    [[nodiscard]] virtual std::size_t link_in_flight() const noexcept = 0;

    [[nodiscard]] virtual WireWordBytes link_word_bytes() const noexcept = 0;

    [[nodiscard]] virtual Ex<proto::FileId> intern_whole(
        std::vector<std::uint8_t>&& bytes,
        std::string_view path
    ) = 0;

    [[nodiscard]] virtual FileTxAnswer file_tx_answer(proto::ActKey act) = 0;

    [[nodiscard]] virtual bool order_load_facts(std::span<const std::uint8_t> facts) = 0;

    [[nodiscard]] virtual Ex<proto::FileId> intern_path(
        std::string_view path,
        proto::FileSize size
    ) = 0;

    [[nodiscard]] virtual proto::ActKey next_generation() noexcept = 0;

    [[nodiscard]] virtual MountStatus mount_status() = 0;

    [[nodiscard]] virtual bool order_walk(
        proto::IoIndex slot,
        std::string_view path,
        proto::ActKey gen
    ) = 0;

    [[nodiscard]] virtual MountStatus walk_status() = 0;

    [[nodiscard]] virtual bool order_savestates(std::string_view image, proto::ActKey gen) = 0;

    [[nodiscard]] virtual proto::ActKey savestates_answered() = 0;
    [[nodiscard]] virtual SaveExtent save_extent() = 0;
    [[nodiscard]] virtual proto::StatusWord status_word() = 0;

protected:
    ILadderHost() = default;
    ILadderHost(const ILadderHost&) = default;
    ILadderHost& operator=(const ILadderHost&) = default;
};

}
