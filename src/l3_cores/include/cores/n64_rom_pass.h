// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/bootcode_sum.h"
#include "cores/core_window.h"
#include "cores/halfword_swap.h"
#include "cores/loader_memo.h"
#include "cores/md5.h"
#include "cores/n64_db_reader.h"
#include "cores/n64_detect.h"
#include "cores/n64_load_facts.h"
#include "cores/n64_rom.h"
#include "cores/n64_rom_facts.h"
#include "cores/n64_row_pass.h"
#include "cores/word_reverse.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/status_cell.h"
#include "proto/status_word.h"
#include "svc/crc32_accumulator.h"
#include "svc/slot_path.h"
#include "svc/types.h"
#include "svc/vfs.h"

namespace mister::cores {
class N64Loader;
}

namespace mister::cores::n64 {

class RomPass final : public IRowPass {
    TASTY_SEAT_RESIDENT(Main);

public:
    using Path = svc::SlotPathText;

    static constexpr std::size_t kHeaderBytes = 4096;
    static constexpr std::size_t kDbChunkBytes = N64DbReader::kChunkBytes;

    RomPass(
        const svc::Vfs& vfs,
        std::string_view core_dir,
        bool turbo,
        LoaderMemo& memo,
        infra::OptRef<const proto::StatusCell> status_cell
    ) noexcept
        : turbo_(turbo),
          memo_(memo),
          reader_(vfs, core_dir),
          status_cell_(status_cell) {}

    void begin(
        LoadFacts& slab,
        proto::FileSize size,
        const proto::StatusWord& status,
        std::string_view path
    );

    void defer_cart(proto::FileSize size) noexcept {
        cart_bytes_ = size;
        rom_pending_ = true;
    }

    void shape(
        const TransferRow& row,
        LoadFacts& slab,
        std::span<std::uint8_t> piece,
        PiecePlace at
    ) override;
    [[nodiscard]] Ex<bool> finish(LoadFacts& slab, infra::OptRef<ICoreWindow> window) override;

private:
    friend class mister::cores::N64Loader;

    void shape_rom_(std::span<std::uint8_t> piece, PiecePlace at);

    [[nodiscard]] infra::OptRef<const IWordOrder> word_order_for_(ByteOrder order) noexcept;
    void normalize_(std::span<std::uint8_t> data, infra::OptRef<const IWordOrder> rule) noexcept;
    void analyze_header_(std::span<const std::uint8_t> head);
    void apply_patches_(const LoadFacts& slab, infra::OptRef<ICoreWindow> window) noexcept;

    bool turbo_;
    LoaderMemo& memo_;
    N64DbReader reader_;

    infra::OptRef<const proto::StatusCell> status_cell_{};

    DetectLadder ladder_{reader_};
    RomFacts facts_{};
    mra::Md5 md5_{};
    Path rom_path_{};

    infra::OptRef<const IWordOrder> rom_rule_{};

    std::array<std::uint8_t, kHeaderBytes> swap_{};

    HalfwordSwap halfword_swap_{};
    WordReverse word_reverse_{};
    BootcodeSum bootcode_sum_{};
    svc::Crc32AccumulatorT<seat_owner::Main> crc_{};
    svc::Tally patches_applied_{};
    std::uint32_t patch_errors_ = 0;
    proto::FileSize cart_bytes_{};
    bool rom_pending_ = false;
};

}
