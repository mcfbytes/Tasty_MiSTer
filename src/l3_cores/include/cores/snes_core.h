// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/core_support.h"
#include "cores/snes_rom.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/image_bracket.h"
#include "proto/image_sink.h"

namespace mister::cores {

class SnesCore final : public Core {
    TASTY_SEAT_RESIDENT(RT);

public:
    SnesCore(const CoreProfile& p, const HostServices& h) : Core(p, h) {}

    static constexpr std::size_t kChunkBytes = 4096;

    [[nodiscard]] std::uint64_t bytes_sent() const noexcept { return bytes_sent_; }
    [[nodiscard]] std::uint32_t transfers() const noexcept { return transfers_; }
    [[nodiscard]] std::uint32_t rom_transforms() const noexcept { return rom_transforms_; }
    [[nodiscard]] std::uint32_t last_header_addr() const noexcept { return last_header_addr_; }
    [[nodiscard]] std::uint16_t last_index() const noexcept { return last_index_; }

private:
    struct Analysis {
        std::uint32_t addr = 0;
        snes::CartSniff sniff{};
    };

    [[nodiscard]] Ex<void> do_init(proto::CoreSession& s) override;
    void on_set_pending_file_ext(std::string_view ext) noexcept override;
    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override { return bytes_sent_; }
    [[nodiscard]] Ex<void> on_file_tx(IoIndex index, svc::IFile& f) override;
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    [[nodiscard]] Ex<void> tx_rom_(IoIndex index, svc::IFile& f, std::uint32_t file_size);
    [[nodiscard]] Ex<void> tx_raw_(IoIndex index, svc::IFile& f, std::uint64_t total);
    [[nodiscard]] Ex<std::size_t> read_window_(svc::IFile& f, std::uint64_t off,
                                               std::span<std::uint8_t> dst);
    [[nodiscard]] Ex<std::uint32_t> score_candidate_(svc::IFile& f, std::uint32_t base,
                                                     std::uint32_t payload, std::uint32_t addr);
    [[nodiscard]] Ex<Analysis> analyze_(svc::IFile& f, std::uint32_t base, std::uint32_t payload);
    [[nodiscard]] Ex<void> stream_mirrored_(proto::ImageBracket& session, svc::IFile& f,
                                            std::uint32_t base, std::uint32_t payload);

    FixedStr<8, StrFit::Clip> ext_{};
    std::uint64_t bytes_sent_ = 0;
    std::uint32_t transfers_ = 0;
    std::uint32_t rom_transforms_ = 0;
    std::uint32_t last_header_addr_ = 0;
    std::uint16_t last_index_ = 0;

    std::uint8_t buf_[kChunkBytes]{};
};

}  // namespace mister::cores
