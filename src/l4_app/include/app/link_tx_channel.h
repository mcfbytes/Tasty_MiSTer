// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "app/file_bytes.h"
#include "app/interned_file.h"
#include "app/link_bytes.h"
#include "app/link_tx_reader.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/link_op.h"

namespace mister::app {

template <SeatTag Producer>
class LinkTxChannel final : public ILinkTxReader {
    TASTY_SEAT_MEDIATOR(Any, RT);
    static_assert(Producer != SeatTag::RT && Producer != SeatTag::Unbound,
                  "the producer is a named seat other than T-RT, which pops");

public:
    using Bytes = LinkBytes<20, kLinkTxSlabBytes, Producer, SeatTag::RT>;

    LinkTxChannel() noexcept = default;
    LinkTxChannel(const LinkTxChannel&) = delete;
    LinkTxChannel& operator=(const LinkTxChannel&) = delete;
    ~LinkTxChannel() = default;

    template <class A>
        requires infra::AlternativeOf<A, proto::LinkOp>
    [[nodiscard]] bool push(const A& alt) noexcept {
        return push(infra::make<proto::LinkOp>(alt));
    }

    [[nodiscard]] bool push(const proto::LinkOp& op) noexcept {
        seat_assert<Producer>(ERR_SITE(), "LinkTxChannel::push off the producer seat");
        const std::uint32_t at = tx_.pushed();
        bytes_.send_staged(at);
        files_.send_staged(at);
        return tx_.push(op);
    }

    [[nodiscard]] std::optional<proto::LinkOp> pop() noexcept {
        TASTY_SEAT_BODY_MEDIATOR(LinkTxChannel, B);
        const std::uint32_t q = tx_.popped();
        bytes_.release_before(q);
        files_.release_before(q);
        auto op = tx_.pop();
        if (op) {
            bytes_.take_sent();
            files_.take_sent();
        }
        return op;
    }

    [[nodiscard]] bool drained() const noexcept { return tx_.pushed() == tx_.popped(); }

    [[nodiscard]] Ex<proto::TxSlabId> intern(std::span<const std::uint8_t> bytes) noexcept {
        const Ex<std::uint16_t> id = bytes_.intern(bytes);
        if (!id) return std::unexpected(id.error());
        return proto::TxSlabId{*id};
    }
    [[nodiscard]] std::span<const std::uint8_t> bytes(proto::TxSlabId id) const noexcept override {
        TASTY_SEAT_BODY_MEDIATOR(LinkTxChannel, B);
        return bytes_.get(id.v);
    }

    [[nodiscard]] Ex<proto::FileId> intern_file(std::vector<std::uint8_t>&& bytes,
                                                std::string_view ext, std::string_view path,
                                                std::uint32_t load_addr, std::uint32_t crc = 0,
                                                std::uint64_t whole = 0, std::uint64_t offset = 0) {
        return files_.intern(std::move(bytes), ext, path, load_addr, crc, whole, offset);
    }
    [[nodiscard]] bool stamp_save(proto::FileId file, proto::FileId save) noexcept {
        return files_.stamp_save(file, save);
    }
    [[nodiscard]] bool stamp_edge(proto::FileId file, ContentEdge edge) noexcept {
        return files_.stamp_edge(file, edge);
    }
    [[nodiscard]] Ex<proto::FileId> intern_file_path(std::string_view path,
                                                     std::uint64_t size_bytes,
                                                     std::uint32_t crc = 0, std::uint32_t aux = 0) {
        return files_.intern_path(path, size_bytes, crc, aux);
    }

    [[nodiscard]] Ex<proto::FileId> intern_file_at(proto::FileId id,
                                                   std::vector<std::uint8_t>&& bytes,
                                                   std::string_view ext, std::string_view path,
                                                   std::uint32_t load_addr) {
        return files_.intern_at(id, std::move(bytes), ext, path, load_addr);
    }

    [[nodiscard]] std::size_t retained_file_bytes() const noexcept {
        return files_.retained_bytes();
    }
    [[nodiscard]] const InternedFile* file(proto::FileId id) const noexcept override {
        TASTY_SEAT_BODY_MEDIATOR(LinkTxChannel, B);
        return files_.get(id);
    }

    [[nodiscard]] const proto::LinkTxRing& ring() const noexcept { return tx_; }

private:
    proto::LinkTxRing tx_{};
    Bytes bytes_{};
    FileBytes<Producer> files_{};
};

}  // namespace mister::app
