// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "cores/host_file_server.h"
#include "cores/mac/blue_scsi_wire.h"
#include "cores/mailbox_req_id.h"
#include "infra/byte_view.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/storage_backend.h"
#include "svc/vfs.h"

namespace mister::cores::mac {

template <HostFileProtocol P>
class ToolboxSlot final : public svc::IStorageBackend {
    TASTY_SEAT_RESIDENT(Io);

public:
    ToolboxSlot(const svc::Vfs& vfs, P protocol)
        : server_(vfs, std::string_view{}, std::move(protocol)),
          request_(kBlueScsiRequestBytes),
          reply_(kBlueScsiReplyBytes) {

        (void)server_.bind_page(reply_).has_value();
        reset_status_();
    }

    ToolboxSlot(const ToolboxSlot&) = delete;
    ToolboxSlot& operator=(const ToolboxSlot&) = delete;

    [[nodiscard]] Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) override {
        TASTY_SEAT_BODY(ToolboxSlot);
        std::memset(dst.data(), 0, dst.size());
        proto::Lba block = block_at_(at);
        for (std::size_t done = 0; done < dst.size();
             done += kBlueScsiBlockBytes, block += kOneBlock) {
            const std::span<std::byte> out = dst.subspan(
                done,
                std::min(kBlueScsiBlockBytes, dst.size() - done)
            );
            if (block == kCommandLba) {
                std::memcpy(out.data(), status_.data(), std::min(out.size(), status_.size()));
            } else if (block - kPayloadLba < kReplyBlocks) {
                const std::size_t from = kBlueScsiBlock.window_bytes(block - kPayloadLba);
                std::memcpy(out.data(), reply_.data() + from, out.size());
            }
        }
        return infra::ByteCount{dst.size()};
    }

    [[nodiscard]] Ex<infra::ByteCount> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) override {
        TASTY_SEAT_BODY(ToolboxSlot);
        proto::Lba block = block_at_(at);
        bool command = false;
        for (std::size_t done = 0; done < src.size();
             done += kBlueScsiBlockBytes, block += kOneBlock) {
            const std::span<const std::byte> in = src.subspan(
                done,
                std::min(kBlueScsiBlockBytes, src.size() - done)
            );
            if (block - kCommandLba > kTailBlocks)
                continue;
            const std::size_t to = kBlueScsiBlock.window_bytes(block - kCommandLba);
            std::memcpy(request_.data() + to, in.data(), in.size());
            command = command || block == kCommandLba;
        }
        if (command)
            run_();
        return infra::ByteCount{src.size()};
    }

    [[nodiscard]] Ex<proto::FileSize> open_extent(
        proto::SlotIndex,
        const svc::BlockCtl::StorageOrder& order
    ) override {
        TASTY_SEAT_BODY(ToolboxSlot);
        const auto text = infra::byte_cast<const char>(order.view());
        server_.protocol().close();
        server_.sandbox().rebind(std::string_view(text.data(), text.size()));
        reset_status_();
        return proto::FileSize{};
    }

    void release(proto::SlotIndex) noexcept override {
        TASTY_SEAT_BODY(ToolboxSlot);
        server_.protocol().close();
    }

private:
    static constexpr proto::Lba kCommandLba{};
    static constexpr proto::Lba kPayloadLba{1};
    static constexpr proto::BlockCount kOneBlock{1};
    static constexpr proto::BlockCount kTailBlocks{kBlueScsiTailBlocks};
    static constexpr proto::BlockCount kReplyBlocks{kBlueScsiReplyBlocks};

    [[nodiscard]] static constexpr proto::Lba block_at_(proto::FileOffset at) noexcept {
        return kCommandLba + (at - proto::kFileStart).whole_units(kBlueScsiBlock);
    }

    void reset_status_() noexcept {
        status_.fill(std::byte{0});
        (void)blue_scsi_status_block(BlueScsiStatus::Check, {}, status_);
    }

    void run_() {
        std::fill(reply_.begin(), reply_.end(), std::byte{0});
        reset_status_();

        edge_ = edge_.next();
        const auto served = server_.on_mailbox_edge(edge_, request_, status_);
        if (!served)
            reset_status_();
    }

    HostFileServer<P> server_;
    std::vector<std::byte> request_;
    std::vector<std::byte> reply_;
    std::array<std::byte, kBlueScsiBlockBytes> status_{};
    MailboxReqId edge_{};
};

}
