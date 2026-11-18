// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "svc/types.h"
#include "app/file_intern.h"
#include "app/file_tx_level.h"
#include "app/link_tx_channel.h"
#include "app/load_key.h"
#include "app/path_text.h"
#include "app/rung.h"
#include "cores/file_tx.h"
#include "cores/loader.h"
#include "cores/payload_pieces.h"
#include "cores/wire_chunk.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/spi_fio_queue.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::app {

struct FileTxStamp {
    proto::FileId save{};
    ContentEdge edge = ContentEdge::Content;
    bool companion = false;

    [[nodiscard]] bool any() const noexcept {
        return save != proto::FileId{} || edge != ContentEdge::Content;
    }
};

class FileTxPieces final : public Rung {
    TASTY_SEAT_RESIDENT(Main);

public:
    enum class Pass : std::uint8_t { Sent, Waiting, Done, Failed };

    [[nodiscard]] static constexpr proto::FileSize piece_bytes(hal::Width w) noexcept {
        return proto::FileSize{
            cores::PayloadPieces::piece_bytes(cores::WireChunk::of(cores::kFileTxChunkBytes), w)
        };
    }

    struct LastOp {
        proto::ActKey act{};
        proto::FileOffset from{};
        svc::Crc32 crc{};
        bool shaped = false;
        bool stamped = false;
        bool remounts_saves = false;
    };

    [[nodiscard]] static Ex<FileTxPieces> start(
        std::unique_ptr<svc::IFile> file,
        proto::FileSize total,
        proto::WideIoIndex wire_index,
        std::string_view path,
        FileTxStamp stamp = {},
        std::unique_ptr<cores::ILoader> loader = nullptr,
        const cores::TransferRow& row = {}
    );

    [[nodiscard]] static Ex<FileTxPieces> resume(
        std::unique_ptr<svc::IFile> file,
        proto::FileSize total,
        proto::WideIoIndex wire_index,
        std::string_view path,
        proto::FileOffset from,
        svc::Crc32 crc
    );

    [[nodiscard]] Pass step(
        LinkTxChannel<SeatTag::Main>& inbox,
        const FileTxLevelCell& level,
        proto::ActKey act,
        hal::Width width
    );

    [[nodiscard]] bool ready(
        const LinkTxChannel<SeatTag::Main>& inbox,
        const FileTxLevelCell& level
    ) const noexcept;

    [[nodiscard]] bool wants_pass(
        const LinkTxChannel<SeatTag::Main>& inbox,
        const FileTxLevelCell& level
    ) const noexcept override;
    [[nodiscard]] Rung::Pass step(const RungEdges& edges, cores::ILadderHost& owner) override;

    [[nodiscard]] proto::FileOffset sent() const noexcept { return sent_; }

    [[nodiscard]] proto::FileSize total() const noexcept { return total_; }

    [[nodiscard]] proto::WideIoIndex wire_index() const noexcept { return wire_index_; }

    [[nodiscard]] std::string_view path() const noexcept { return path_.view(); }

    [[nodiscard]] LastOp last_op() const noexcept { return last_; }

    [[nodiscard]] bool companion_follows() const noexcept { return stamp_.companion; }

    [[nodiscard]] std::unique_ptr<svc::IFile> take_file() noexcept { return std::move(file_); }

    [[nodiscard]] std::uint32_t rewinds() const noexcept { return rewinds_; }

    [[nodiscard]] bool resumed() const noexcept { return crc_.has_value(); }

private:
    FileTxPieces(
        std::unique_ptr<svc::IFile> file,
        proto::FileSize total,
        proto::WideIoIndex wire_index,
        FileTxStamp stamp,
        std::unique_ptr<cores::ILoader> loader,
        const cores::TransferRow& row
    )
        : file_(std::move(file)),
          loader_(std::move(loader)),
          row_(row),
          total_(total),
          key_(total.file_word<std::uint64_t>()),
          wire_index_(wire_index),
          stamp_(stamp) {}

    struct Outstanding {
        proto::ActKey act{};
        proto::FileOffset from{};
        proto::FileSize bytes{};
    };

    [[nodiscard]] std::optional<FileTxLevel> answer_(const FileTxLevelCell& level) const noexcept;

    [[nodiscard]] bool stage_(proto::FileSize piece);

    [[nodiscard]] Pass fail_(LinkTxChannel<SeatTag::Main>& inbox);
    [[nodiscard]] Pass cut_(LinkTxChannel<SeatTag::Main>& inbox);

    std::unique_ptr<svc::IFile> file_;
    std::unique_ptr<cores::ILoader> loader_;
    cores::TransferRow row_;
    std::optional<FileIntern> staged_{};
    proto::FileSize total_;
    proto::FileOffset sent_{};
    proto::FileOffset keyed_to_{};
    LoadKey key_;
    std::optional<svc::Crc32> crc_{};
    proto::WideIoIndex wire_index_;
    FileTxStamp stamp_;
    std::optional<hal::Width> width_{};
    bool cut_owed_ = false;
    bool facts_sent_ = false;
    std::uint32_t pieces_ = 0;
    std::uint32_t rewinds_ = 0;
    std::optional<Outstanding> outstanding_{};
    LastOp last_{};
    std::optional<std::uint32_t> slab_full_at_{};
    PathText path_{};
};

}
