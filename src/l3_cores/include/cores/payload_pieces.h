// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cores/ladder_host.h"
#include "cores/wire_chunk.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/link_op.h"
#include "proto/link/spi_fio_queue.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::cores {

class PayloadPieces {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr std::size_t kPieceWords = proto::link::tightest_load_budget_words();

    static constexpr std::size_t kPieceBytes = kPieceWords;

    static constexpr std::size_t kAhead = 6;

    enum class Pass : std::uint8_t { Waiting, Done };

    struct Frame {
        proto::WideIoIndex dest{};
        proto::RegionCode region = proto::RegionCode::kNone;
        WireChunk chunk = WireChunk::single();
        bool progress = false;
        proto::ActKey16 act{};
    };

    [[nodiscard]] static Ex<PayloadPieces> of_file(
        std::unique_ptr<svc::IFile> file,
        proto::FileSize size,
        std::string_view ext,
        const Frame& frame
    );

    [[nodiscard]] static Ex<PayloadPieces> of_bytes(
        std::vector<std::uint8_t> bytes,
        std::string_view ext,
        const Frame& frame
    );

    struct HeldFrame {
        proto::WideIoIndex dest{};
        hal::Width framing{};
        proto::FioWordCount piece{};
    };

    [[nodiscard]] static Ex<PayloadPieces> held_window(
        std::vector<std::uint8_t> bytes,
        const HeldFrame& frame
    );

    [[nodiscard]] static constexpr std::size_t piece_bytes(
        WireChunk chunk,
        hal::Width word
    ) noexcept {
        const std::size_t cap = kPieceWords * hal::word_bytes(word);
        if (chunk.one_write())
            return cap;
        return chunk.whole_within(cap);
    }

    [[nodiscard]] Ex<Pass> step(ILadderHost& host);

    [[nodiscard]] proto::FileSize size() const noexcept { return size_; }

    [[nodiscard]] proto::FileSize sent() const noexcept { return sent_ - proto::kFileStart; }

    [[nodiscard]] proto::ActKey16 act() const noexcept { return frame_.act; }

private:
    PayloadPieces(
        std::unique_ptr<svc::IFile> file,
        std::vector<std::uint8_t> bytes,
        proto::FileSize size,
        std::string_view ext,
        const Frame& frame
    )
        : file_(std::move(file)),
          bytes_(std::move(bytes)),
          size_(size),
          ext_(ext),
          frame_(frame) {}

    [[nodiscard]] Ex<void> stage_(std::size_t n);
    [[nodiscard]] Ex<Pass> fail_(ILadderHost& host);

    [[nodiscard]] proto::LinkOp piece_op_(proto::FileId id, proto::LinkOp::StagePhase phase);

    std::unique_ptr<svc::IFile> file_;
    std::vector<std::uint8_t> bytes_;
    std::optional<std::vector<std::uint8_t>> staged_{};
    proto::FileSize size_{};
    proto::FileOffset sent_{};
    std::optional<hal::Width> width_{};
    std::string ext_;
    Frame frame_;
    std::optional<HeldFrame> held_{};
    Pass end_ = Pass::Waiting;
    std::optional<Error> failure_{};
    bool cut_owed_ = false;
};

}
