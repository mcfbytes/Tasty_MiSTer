// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "app/file_tx_level.h"
#include "app/link_tx_channel.h"
#include "app/load_key.h"
#include "app/path_text.h"
#include "cores/loader.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "svc/file.h"

namespace mister::app {

class FileTxPieces {
    TASTY_SEAT_RESIDENT(Main);

public:
    enum class Pass : std::uint8_t { Sent, Waiting, Done, Failed };

    struct LastOp {
        std::uint32_t act = 0;
        std::uint64_t from = 0;
        std::uint32_t crc = 0;
        bool shaped = false;
    };

    [[nodiscard]] static Ex<FileTxPieces> start(std::unique_ptr<svc::IFile> file,
                                                std::uint64_t total, std::uint8_t wire_index,
                                                std::string_view path,
                                                std::unique_ptr<cores::ILoader> loader = nullptr,
                                                const cores::TransferRow& row = {});

    [[nodiscard]] static Ex<FileTxPieces> resume(std::unique_ptr<svc::IFile> file,
                                                 std::uint64_t total, std::uint8_t wire_index,
                                                 std::string_view path, std::uint64_t from,
                                                 std::uint32_t crc);

    [[nodiscard]] Pass step(LinkTxChannel<SeatTag::Main>& inbox, const FileTxLevelCell& level,
                            std::uint32_t act);

    [[nodiscard]] bool ready(const LinkTxChannel<SeatTag::Main>& inbox,
                             const FileTxLevelCell& level) const noexcept;

    [[nodiscard]] std::uint64_t sent() const noexcept { return sent_; }
    [[nodiscard]] std::uint64_t total() const noexcept { return total_; }
    [[nodiscard]] std::uint8_t wire_index() const noexcept { return wire_index_; }
    [[nodiscard]] std::string_view path() const noexcept { return path_.view(); }
    [[nodiscard]] LastOp last_op() const noexcept { return last_; }

    [[nodiscard]] std::unique_ptr<svc::IFile> take_file() noexcept { return std::move(file_); }

    [[nodiscard]] std::uint32_t rewinds() const noexcept { return rewinds_; }

    [[nodiscard]] bool resumed() const noexcept { return crc_.has_value(); }

private:
    FileTxPieces(std::unique_ptr<svc::IFile> file, std::uint64_t total, std::uint8_t wire_index,
                 std::unique_ptr<cores::ILoader> loader, const cores::TransferRow& row)
        : file_(std::move(file)), loader_(std::move(loader)), row_(row), total_(total), key_(total),
          wire_index_(wire_index) {}

    struct Outstanding {
        std::uint32_t act = 0;
        std::uint64_t from = 0;
        std::uint64_t bytes = 0;
    };

    [[nodiscard]] std::optional<FileTxLevel> answer_(const FileTxLevelCell& level) const noexcept;

    [[nodiscard]] bool stage_();

    [[nodiscard]] Pass fail_(LinkTxChannel<SeatTag::Main>& inbox);
    [[nodiscard]] Pass cut_(LinkTxChannel<SeatTag::Main>& inbox);

    std::unique_ptr<svc::IFile> file_;
    std::unique_ptr<cores::ILoader> loader_;
    cores::TransferRow row_;
    std::optional<std::vector<std::uint8_t>> staged_{};
    std::uint64_t total_;
    std::uint64_t sent_ = 0;
    std::uint64_t keyed_to_ = 0;
    LoadKey key_;
    std::optional<std::uint32_t> crc_{};
    std::uint8_t wire_index_;
    bool cut_owed_ = false;
    bool facts_sent_ = false;
    std::uint32_t pieces_ = 0;
    std::uint32_t rewinds_ = 0;
    std::optional<Outstanding> outstanding_{};
    LastOp last_{};
    std::optional<std::uint32_t> slab_full_at_{};
    PathText path_{};
};

}  // namespace mister::app
