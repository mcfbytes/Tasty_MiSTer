// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "app/file_bytes.h"
#include "app/link_bytes.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/link_op.h"

namespace mister::app {

class LinkTxChannel {
    TASTY_SEAT_MEDIATOR(Any, RT);

public:
    using Bytes = LinkBytes<20, 256>;

    template <class A>
        requires infra::AlternativeOf<A, proto::LinkOp>
    [[nodiscard]] bool push(const A& alt) noexcept {
        return push(infra::make<proto::LinkOp>(alt));
    }
    [[nodiscard]] bool push(const proto::LinkOp& op) noexcept { return tx_.push(op); }
    [[nodiscard]] std::optional<proto::LinkOp> pop() noexcept { return tx_.pop(); }

    [[nodiscard]] Ex<proto::TxSlabId> intern(std::span<const std::uint8_t> bytes) noexcept {
        return bytes_.intern(bytes, tx_.pushed(), tx_.popped()).transform([](std::uint16_t v) {
            return proto::TxSlabId{v};
        });
    }

    [[nodiscard]] std::span<const std::uint8_t> bytes(proto::TxSlabId id) const noexcept {
        return bytes_.get(id.v);
    }

    [[nodiscard]] Ex<proto::FileId> intern_file(std::vector<std::uint8_t>&& bytes,
                                                std::string_view ext, std::string_view path,
                                                std::uint32_t load_addr, std::uint32_t crc = 0,
                                                std::uint64_t whole = 0, std::uint64_t offset = 0) {
        return files_.intern(std::move(bytes), ext, path, load_addr, tx_.pushed(), tx_.popped(),
                             crc, whole, offset);
    }
    [[nodiscard]] bool stamp_save(proto::FileId file, proto::FileId save) noexcept {
        return files_.stamp_save(file, save);
    }
    [[nodiscard]] Ex<proto::FileId> intern_file_path(std::string_view path,
                                                     std::uint64_t size_bytes) {
        return files_.intern_path(path, size_bytes, tx_.pushed(), tx_.popped());
    }

    [[nodiscard]] Ex<proto::FileId> intern_file_at(proto::FileId id,
                                                   std::vector<std::uint8_t>&& bytes,
                                                   std::string_view ext, std::string_view path,
                                                   std::uint32_t load_addr) {
        return files_.intern_at(id, std::move(bytes), ext, path, load_addr);
    }

    [[nodiscard]] const FileBytes::Slot* file(proto::FileId id) const noexcept {
        return files_.get(id);
    }

    [[nodiscard]] proto::LinkTxRing& ring() noexcept { return tx_; }
    [[nodiscard]] const proto::LinkTxRing& ring() const noexcept { return tx_; }

private:
    proto::LinkTxRing tx_{};
    Bytes bytes_{};
    FileBytes files_{};
};

}  // namespace mister::app
