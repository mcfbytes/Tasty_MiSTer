// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "cores/core_window.h"
#include "cores/load_ask.h"
#include "cores/n64_load_facts.h"
#include "cores/n64_row_pass.h"
#include "cores/n64dd_disk_map.h"
#include "cores/n64dd_expander.h"
#include "cores/n64dd_system_area.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/slot_path.h"
#include "svc/vfs.h"

namespace mister::cores {
class N64Loader;
}

namespace mister::cores::n64 {

class DiskPass final : public IRowPass {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr std::size_t kRamStepBytes = 64u * 1024u;
    using Path = svc::SlotPathText;

    [[nodiscard]] static Ex<std::unique_ptr<DiskPass>> open(
        const svc::Vfs& vfs,
        const LoadAsk& ask,
        std::string_view core_dir
    );

    [[nodiscard]] const n64dd::SystemArea& area() const noexcept { return area_; }

    [[nodiscard]] bool expanded() const noexcept { return expander_.complete(); }

    [[nodiscard]] std::string_view ram_path() const noexcept { return ram_path_.view(); }

    [[nodiscard]] std::string_view path() const noexcept { return path_.view(); }

    void shape(const TransferRow&, LoadFacts&, std::span<std::uint8_t>, PiecePlace) override {
        TASTY_SEAT_BODY(DiskPass);
    }

    [[nodiscard]] Ex<void> place(std::span<const std::uint8_t> piece, ICoreWindow& window) override;
    [[nodiscard]] Ex<bool> conclude(ICoreWindow& window) override;
    [[nodiscard]] Ex<bool> finish(LoadFacts& slab, infra::OptRef<ICoreWindow> window) override;

private:
    friend class mister::cores::N64Loader;

    explicit DiskPass(const svc::Vfs& vfs) noexcept : vfs_(vfs) {}

    [[nodiscard]] Ex<bool> load_ram_step_(ICoreWindow& window);

    const svc::Vfs& vfs_;
    n64dd::SystemArea area_{};
    n64dd::DiskMap map_{};
    n64dd::NddExpander expander_{};
    Path path_{};
    Path ram_path_{};
    hal::FabricAddr load_addr_{};
    std::unique_ptr<svc::IFile> ram_file_{};
    proto::FileSize ram_off{};
    proto::FileSize ram_loaded{};
    bool ram_checked = false;
    std::array<std::uint8_t, kRamStepBytes> ram_buf{};
};

}
