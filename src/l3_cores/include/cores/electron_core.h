// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cores/core_support.h"
#include "cores/uef.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/image_bracket.h"
#include "proto/image_sink.h"

namespace mister::cores {

class ElectronCore final : public Core {
    TASTY_SEAT_RESIDENT(RT);

public:
    ElectronCore(const CoreProfile& p, const HostServices& h) : Core(p, h) {}

    static constexpr std::size_t kChunkBytes = 4096;

    static constexpr std::size_t kMaxImageBytes = 16u * 1024u * 1024u;

    [[nodiscard]] std::uint64_t bytes_sent() const noexcept { return bytes_sent_; }

    [[nodiscard]] std::uint32_t transfers() const noexcept { return transfers_; }

    [[nodiscard]] std::uint32_t last_numbits() const noexcept { return last_numbits_; }

    [[nodiscard]] std::uint16_t last_index() const noexcept { return last_index_; }

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;
    void on_set_pending_file_ext(std::string_view ext) noexcept override;

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override { return bytes_sent_; }

    [[nodiscard]] Ex<void> on_file_tx(IoIndex index, std::span<const std::uint8_t> bytes) override;

    [[nodiscard]] Ex<void> tx_uef_(IoIndex index, std::span<const std::uint8_t> bytes);
    [[nodiscard]] Ex<void> tx_raw_(IoIndex index, std::span<const std::uint8_t> bytes);

    [[nodiscard]] Ex<bool> materialize_(std::span<const std::uint8_t> bytes);

    FixedStr<8, StrFit::Clip> ext_{};
    std::uint64_t bytes_sent_ = 0;
    std::uint32_t transfers_ = 0;
    std::uint32_t last_numbits_ = 0;
    std::uint16_t last_index_ = 0;
    std::vector<std::uint8_t> image_;
    std::uint8_t buf_[kChunkBytes]{};
};

}
