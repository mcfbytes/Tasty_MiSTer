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
#include "cores/wire_word.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/link_op.h"
#include "proto/spi_fio_queue.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::cores {

class PayloadPieces {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr std::size_t kPieceWords = proto::tightest_load_budget_words();

    static constexpr std::size_t kPieceBytes = kPieceWords;

    static constexpr std::size_t kAhead = 6;

    enum class Pass : std::uint8_t { Waiting, Done, Failed };

    struct Frame {
        proto::WideIoIndex dest{};
        proto::RegionCode region = proto::RegionCode::kNone;
        WireChunk chunk = WireChunk::single();
        bool progress = false;
        proto::ActKey16 act{};
    };

    [[nodiscard]] static Ex<PayloadPieces> of_file(
        std::unique_ptr<svc::IFile> file,
        std::uint64_t size,
        std::string_view ext,
        const Frame& frame
    );

    [[nodiscard]] static Ex<PayloadPieces> of_bytes(
        std::vector<std::uint8_t> bytes,
        std::string_view ext,
        const Frame& frame
    );

    [[nodiscard]] static constexpr std::size_t piece_bytes(
        WireChunk chunk,
        WireWordBytes word
    ) noexcept {
        const std::size_t cap = word.scale(kPieceWords);
        if (chunk.one_write())
            return cap;
        return chunk.whole_within(cap);
    }

    [[nodiscard]] Pass step(ILadderHost& host);

    [[nodiscard]] std::uint64_t size() const noexcept { return size_; }

    [[nodiscard]] std::uint64_t sent() const noexcept { return sent_; }

private:
    PayloadPieces(
        std::unique_ptr<svc::IFile> file,
        std::vector<std::uint8_t> bytes,
        std::uint64_t size,
        std::string_view ext,
        const Frame& frame
    )
        : file_(std::move(file)),
          bytes_(std::move(bytes)),
          size_(size),
          ext_(ext),
          frame_(frame) {}

    [[nodiscard]] bool stage_(std::size_t n);
    [[nodiscard]] Pass fail_(ILadderHost& host);

    std::unique_ptr<svc::IFile> file_;
    std::vector<std::uint8_t> bytes_;
    std::optional<std::vector<std::uint8_t>> staged_{};
    std::uint64_t size_ = 0;
    std::uint64_t sent_ = 0;
    std::optional<WireWordBytes> word_bytes_{};
    std::string ext_;
    Frame frame_;
    Pass end_ = Pass::Waiting;
    bool cut_owed_ = false;
};

}
