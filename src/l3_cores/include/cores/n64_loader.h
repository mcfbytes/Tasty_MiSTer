// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/core_window.h"
#include "cores/loader.h"
#include "cores/loader_memo.h"
#include "cores/n64_disk_pass.h"
#include "cores/n64_ipl_pass.h"
#include "cores/n64_load_facts.h"
#include "cores/n64_pass_kind.h"
#include "cores/n64_rom_pass.h"
#include "cores/n64_row_pass.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/status_cell.h"
#include "proto/types.h"
#include "svc/slot_path.h"
#include "svc/types.h"
#include "svc/vfs.h"

namespace mister::cores {

class N64Loader final : public ILoader {
    TASTY_SEAT_RESIDENT(Main);

public:
    using Path = svc::SlotPathText;

    using RomFacts = n64::RomFacts;

    N64Loader(
        const svc::Vfs& vfs,
        std::string_view core_dir,
        bool turbo,
        LoaderMemo& memo,
        infra::OptRef<const proto::StatusCell> status_cell = {}
    ) noexcept
        : vfs_(vfs),
          core_dir_(core_dir),
          memo_(memo),
          rom_(vfs, core_dir, turbo, memo, status_cell),
          ipl_(vfs, memo) {}

    [[nodiscard]] Ex<LoadPlan> plan(const LoadAsk& ask, const proto::StatusWord& status) override;
    void shape(const TransferRow& row, std::span<std::uint8_t> piece, std::uint64_t off) override;
    [[nodiscard]] Ex<void> place(
        const TransferRow& row,
        std::span<const std::uint8_t> piece,
        std::uint64_t off,
        ICoreWindow& window
    ) override;
    [[nodiscard]] Ex<bool> conclude(const TransferRow& row, ICoreWindow& window) override;
    [[nodiscard]] Ex<void> finish(infra::OptRef<ICoreWindow> window) override;
    [[nodiscard]] std::span<const std::uint8_t> facts() const noexcept override;

    [[nodiscard]] infra::OptRef<const n64dd::SystemArea> disk_area() const noexcept {
        if (!disk_)
            return {};
        return infra::OptRef<const n64dd::SystemArea>{disk_->area()};
    }

    [[nodiscard]] bool disk_expanded() const noexcept { return disk_ && disk_->expanded(); }

    [[nodiscard]] proto::FileSize ram_save_loaded() const noexcept {
        return disk_ ? disk_->ram_loaded : proto::FileSize{};
    }

    [[nodiscard]] std::string_view ram_save_path() const noexcept {
        return disk_ ? disk_->ram_path() : std::string_view{};
    }

    [[nodiscard]] const RomFacts& rom_facts() const noexcept { return rom_.facts_; }

    [[nodiscard]] std::string_view auto_detect_info() const noexcept { return rom_.ladder_.info(); }

    [[nodiscard]] std::uint32_t db_long_lines() const noexcept { return rom_.reader_.long_lines(); }

    [[nodiscard]] svc::Tally patches_applied() const noexcept { return rom_.patches_applied_; }

    [[nodiscard]] std::uint32_t patch_errors() const noexcept { return rom_.patch_errors_; }

    [[nodiscard]] const n64::LoadFacts& load_facts() const noexcept { return slab_; }

private:
    [[nodiscard]] Ex<LoadPlan> plan_rom_(const LoadAsk& ask, const proto::StatusWord& status);
    [[nodiscard]] Ex<LoadPlan> plan_disk_(const LoadAsk& ask, const proto::StatusWord& status);

    void plan_autoload_(LoadPlan& p);

    [[nodiscard]] n64::IRowPass* pass_(n64::PassKind kind) noexcept;

    const svc::Vfs& vfs_;
    std::string_view core_dir_;
    LoaderMemo& memo_;
    n64::LoadFacts slab_{};
    bool concluded_ = false;
    bool bad_row_ = false;
    Path cart_path_{};
    n64::RomPass rom_;
    n64::IplPass ipl_;
    std::unique_ptr<n64::DiskPass> disk_{};

    std::optional<n64::PassKind> current_{};
};

}
