// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cores/bootcode_sum.h"
#include "cores/core_window.h"
#include "cores/halfword_swap.h"
#include "cores/loader_memo.h"
#include "cores/n64_load_facts.h"
#include "cores/n64_rom.h"
#include "cores/n64_row_pass.h"
#include "cores/word_reverse.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/slot_path.h"
#include "svc/vfs.h"

namespace mister::cores {
class N64Loader;
}

namespace mister::cores::n64 {

class IplPass final : public IRowPass {
    TASTY_SEAT_RESIDENT(Main);

public:
    using Path = svc::SlotPathText;

    IplPass(const svc::Vfs& vfs, LoaderMemo& memo) noexcept : vfs_(vfs), memo_(memo) {}

    [[nodiscard]] bool plan_row(TransferRow& row, std::string_view path, bool boot);

    void shape(
        const TransferRow&,
        LoadFacts& slab,
        std::span<std::uint8_t> piece,
        PiecePlace at
    ) override;
    [[nodiscard]] Ex<bool> finish(LoadFacts& slab, infra::OptRef<ICoreWindow> window) override;

private:
    friend class mister::cores::N64Loader;

    void shape_ipl_(LoadFacts& slab, std::span<std::uint8_t> piece, PiecePlace at);
    [[nodiscard]] infra::OptRef<const IWordOrder> word_order_for_(ByteOrder order) noexcept;
    void normalize_(std::span<std::uint8_t> data, infra::OptRef<const IWordOrder> rule) noexcept;
    void remember_boot_(std::string_view path) noexcept;

    const svc::Vfs& vfs_;
    LoaderMemo& memo_;
    Path ipl_path_{};

    infra::OptRef<const IWordOrder> ipl_rule_{};
    bool ipl_boot_ = false;

    HalfwordSwap halfword_swap_{};
    WordReverse word_reverse_{};
    BootcodeSum bootcode_sum_{};
};

}
