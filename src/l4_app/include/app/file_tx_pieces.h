// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "app/link_tx_channel.h"
#include "app/load_key.h"
#include "app/path_text.h"
#include "cores/loader.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "svc/file.h"

namespace mister::app {

class FileTxPieces {
    TASTY_SEAT_EXEMPT(main);

public:
    enum class Pass : std::uint8_t { Sent, Waiting, Done, Failed };

    static constexpr std::uint32_t kAhead = 2;

    [[nodiscard]] static Ex<FileTxPieces> start(std::unique_ptr<svc::IFile> file,
                                                std::uint64_t total, std::uint8_t wire_index,
                                                std::string_view path,
                                                std::unique_ptr<cores::ILoader> loader = nullptr,
                                                const cores::TransferRow& row = {});

    [[nodiscard]] Pass step(LinkTxChannel& inbox);

    [[nodiscard]] bool ready(const LinkTxChannel& inbox) const noexcept;

    [[nodiscard]] std::uint64_t sent() const noexcept { return sent_; }
    [[nodiscard]] std::uint64_t total() const noexcept { return total_; }

private:
    FileTxPieces(std::unique_ptr<svc::IFile> file, std::uint64_t total, std::uint8_t wire_index,
                 std::unique_ptr<cores::ILoader> loader, const cores::TransferRow& row)
        : file_(std::move(file)), loader_(std::move(loader)), row_(row), total_(total), key_(total),
          wire_index_(wire_index) {}

    [[nodiscard]] bool stage_();

    [[nodiscard]] Pass fail_(LinkTxChannel& inbox);
    [[nodiscard]] Pass cut_(LinkTxChannel& inbox);

    std::unique_ptr<svc::IFile> file_;
    std::unique_ptr<cores::ILoader> loader_;
    cores::TransferRow row_;
    std::optional<std::vector<std::uint8_t>> staged_{};
    std::uint64_t total_;
    std::uint64_t sent_ = 0;
    LoadKey key_;
    std::uint8_t wire_index_;
    bool cut_owed_ = false;
    bool facts_sent_ = false;
    std::uint32_t pieces_ = 0;
    std::uint32_t last_at_ = 0;
    std::uint32_t prev_at_ = 0;
    std::optional<std::uint32_t> slab_full_at_{};
    PathText path_{};
};

}  // namespace mister::app
