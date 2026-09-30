// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/mount_status.h"
#include "cores/save_extent.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "proto/link_op.h"
#include "proto/status_word.h"

namespace mister::cores {

class ILadderHost {
public:
    virtual ~ILadderHost() = default;

    [[nodiscard]] virtual bool order(const proto::LinkOp& op) = 0;
    template <class A>
        requires infra::AlternativeOf<A, proto::LinkOp>
    [[nodiscard]] bool order(const A& alt) {
        return order(infra::make<proto::LinkOp>(alt));
    }

    [[nodiscard]] virtual Ex<proto::FileId> intern_bytes(std::span<const std::uint8_t> bytes,
                                                         std::string_view ext,
                                                         std::uint32_t load_addr) = 0;

    [[nodiscard]] virtual Ex<proto::FileId> intern_piece(std::span<const std::uint8_t> bytes,
                                                         std::string_view ext, std::uint64_t,
                                                         std::uint64_t) {
        return intern_bytes(bytes, ext, 0);
    }

    [[nodiscard]] virtual std::size_t link_in_flight() const noexcept { return 0; }

    [[nodiscard]] virtual std::size_t link_word_bytes() const noexcept { return 1; }

    [[nodiscard]] virtual Ex<proto::FileId> intern_path(std::string_view path,
                                                        std::uint64_t size_bytes) = 0;

    [[nodiscard]] virtual std::uint32_t next_generation() noexcept = 0;

    [[nodiscard]] virtual MountStatus mount_status() = 0;

    [[nodiscard]] virtual bool order_walk(proto::IoIndex, std::string_view, std::uint32_t) {
        return false;
    }

    [[nodiscard]] virtual MountStatus walk_status() { return {}; }
    [[nodiscard]] virtual SaveExtent save_extent() = 0;
    [[nodiscard]] virtual proto::StatusWord status_word() = 0;

    virtual void bios_missing() = 0;

protected:
    ILadderHost() = default;
    ILadderHost(const ILadderHost&) = default;
    ILadderHost& operator=(const ILadderHost&) = default;
};

}  // namespace mister::cores
