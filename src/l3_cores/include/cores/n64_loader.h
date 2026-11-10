// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "cores/core_window.h"
#include "cores/loader.h"
#include "cores/loader_memo.h"
#include "cores/md5.h"
#include "cores/n64_db_reader.h"
#include "cores/n64_detect.h"
#include "cores/n64_file_route.h"
#include "cores/bootcode_sum.h"
#include "cores/halfword_swap.h"
#include "cores/n64_ipl3.h"
#include "cores/n64_load_facts.h"
#include "cores/n64_patch_table.h"
#include "cores/n64_rom.h"
#include "cores/word_reverse.h"
#include "cores/n64dd_disk_map.h"
#include "cores/n64dd_expander.h"
#include "cores/n64dd_ram_save.h"
#include "cores/n64dd_system_area.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/status_cell.h"
#include "proto/status_word.h"
#include "svc/file.h"
#include "svc/slot_path.h"
#include "svc/types.h"
#include "svc/vfs.h"

namespace mister::cores {

class N64Loader final : public ILoader {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr std::size_t kHeaderBytes = 4096;
    static constexpr std::size_t kDbChunkBytes = N64DbReader::kChunkBytes;

    enum class Shape : std::uint8_t { Rom, Ipl, Disk };

    static constexpr std::size_t kRamStepBytes = 64u * 1024u;
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
          turbo_(turbo),
          memo_(memo),
          reader_(vfs, core_dir),
          status_cell_(status_cell) {}

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
        return infra::OptRef<const n64dd::SystemArea>{disk_->area};
    }

    [[nodiscard]] bool disk_expanded() const noexcept {
        return disk_ && disk_->expander.complete();
    }

    [[nodiscard]] std::uint32_t ram_save_loaded() const noexcept {
        return disk_ ? disk_->ram_loaded : 0;
    }

    [[nodiscard]] std::string_view ram_save_path() const noexcept {
        return disk_ ? disk_->ram_path.view() : std::string_view{};
    }

    [[nodiscard]] const RomFacts& rom_facts() const noexcept { return facts_; }

    [[nodiscard]] std::string_view auto_detect_info() const noexcept { return ladder_.info(); }

    [[nodiscard]] std::uint32_t db_long_lines() const noexcept { return reader_.long_lines(); }

    [[nodiscard]] std::uint32_t patches_applied() const noexcept { return patches_applied_; }

    [[nodiscard]] std::uint32_t patch_errors() const noexcept { return patch_errors_; }

    [[nodiscard]] const n64::LoadFacts& load_facts() const noexcept { return slab_; }

private:
    struct DiskLoad {
        n64dd::SystemArea area{};
        n64dd::DiskMap map{};
        n64dd::NddExpander expander{};
        Path path{};
        Path ram_path{};
        hal::FabricAddr load_addr{};
        std::unique_ptr<svc::IFile> ram_file{};
        std::uint64_t ram_off = 0;
        std::uint32_t ram_loaded = 0;
        bool ram_checked = false;
        std::array<std::uint8_t, kRamStepBytes> ram_buf{};
    };

    [[nodiscard]] Ex<LoadPlan> plan_rom_(const LoadAsk& ask, const proto::StatusWord& status);
    [[nodiscard]] Ex<LoadPlan> plan_disk_(const LoadAsk& ask, const proto::StatusWord& status);

    void plan_autoload_(LoadPlan& p);
    [[nodiscard]] bool plan_ipl_row_(TransferRow& row, std::string_view path, bool boot);

    void begin_rom_(proto::FileSize size, const proto::StatusWord& status);
    void shape_rom_(std::span<std::uint8_t> piece, std::uint64_t off);
    void shape_ipl_(std::span<std::uint8_t> piece, std::uint64_t off);
    [[nodiscard]] Ex<bool> load_ram_step_(ICoreWindow& window);
    void remember_boot_(std::string_view path) noexcept;
    void analyze_header_(std::span<const std::uint8_t> head);
    void apply_patches_(infra::OptRef<ICoreWindow> window) noexcept;

    [[nodiscard]] infra::OptRef<const n64::IWordOrder> word_order_for_(
        n64::ByteOrder order
    ) noexcept;
    void normalize_(
        std::span<std::uint8_t> data,
        infra::OptRef<const n64::IWordOrder> rule
    ) noexcept;

    [[nodiscard]] static constexpr std::uint8_t raw(Shape s) noexcept {
        return static_cast<std::uint8_t>(s);
    }

    [[nodiscard]] static Shape shape_of(const TransferRow& row) noexcept;

    const svc::Vfs& vfs_;
    std::string_view core_dir_;
    bool turbo_;
    LoaderMemo& memo_;
    N64DbReader reader_;

    infra::OptRef<const proto::StatusCell> status_cell_{};
    std::unique_ptr<DiskLoad> disk_{};
    Path ipl_path_{};
    Path cart_path_{};
    Path rom_path_{};
    proto::FileSize cart_bytes_{};

    infra::OptRef<const n64::IWordOrder> rom_rule_{};
    infra::OptRef<const n64::IWordOrder> ipl_rule_{};
    Shape shape_ = Shape::Rom;
    bool rom_pending_ = false;
    bool ipl_boot_ = false;
    RomFacts facts_{};
    n64::LoadFacts slab_{};

    n64::HalfwordSwap halfword_swap_{};
    n64::WordReverse word_reverse_{};
    n64::BootcodeSum bootcode_sum_{};

    n64::DetectLadder ladder_{reader_};
    mra::Md5 md5_{};
    std::uint32_t patches_applied_ = 0;
    std::uint32_t patch_errors_ = 0;
    bool concluded_ = false;

    std::uint8_t swap_[kHeaderBytes]{};
};

}
