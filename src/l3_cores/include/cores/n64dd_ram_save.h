// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "cores/core_window.h"
#include "cores/n64dd_disk_map.h"
#include "cores/n64dd_geometry.h"
#include "cores/n64dd_system_area.h"
#include "cores/window_job.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "svc/file.h"
#include "svc/slot_path.h"
#include "svc/vfs.h"

namespace mister::cores::n64dd {

using SavePath = FixedStr<svc::kSlotPathCap, StrFit::Reject>;

[[nodiscard]] bool ram_save_path(
    std::string_view disk_path,
    std::string_view core_dir,
    SavePath& out
) noexcept;

inline constexpr std::uint32_t kFlatBlocks = 2u * kTracksPerHead * 2u;

class RamSaveJob final : public IWindowJob {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr std::size_t kStepBytes = 256u * 1024u;

    struct Order {
        std::uint32_t load_addr = 0;
        std::uint16_t path_len = 0;
        std::uint8_t core_len = 0;
        std::uint8_t pad = 0;
        char disk_path[svc::kSlotPathCap];
        char core_dir[32];
    };

    static_assert(std::is_trivially_copyable_v<Order> && sizeof(Order) <= kWindowOrderBytes);

    class Kind final : public IWindowJobKind {
        TASTY_SEAT_RESIDENT(Io);

    public:
        [[nodiscard]] IWindowJob* build(
            std::span<std::byte> arena,
            std::span<const std::byte> order
        ) const noexcept override;
    };

    RamSaveJob(
        std::uint32_t load_addr,
        std::string_view disk_path,
        std::string_view core_dir
    ) noexcept;

    [[nodiscard]] std::uint32_t window_addr() const noexcept override;
    [[nodiscard]] std::uint32_t window_len() const noexcept override;
    [[nodiscard]] Ex<bool> step(ICoreWindow& window, const svc::Vfs& vfs) override;

    [[nodiscard]] bool wrote() const noexcept { return wrote_; }

    [[nodiscard]] bool matched() const noexcept { return matched_; }

    [[nodiscard]] std::uint32_t markers_cleared() const noexcept { return cleared_; }

private:
    enum class Phase : std::uint8_t { Open, Compare, Write, Clear, Done };

    struct Work {
        SystemArea area{};
        DiskMap map{};
        std::bitset<kFlatBlocks> dirty{};
        SavePath ram_path{};
        std::unique_ptr<svc::IFile> in{};
        std::unique_ptr<svc::IFile> out{};
        std::array<std::byte, kStepBytes> image;
        std::array<std::byte, kStepBytes> file;
    };

    [[nodiscard]] Ex<void> open_(ICoreWindow& window, const svc::Vfs& vfs);
    [[nodiscard]] Ex<void> gather_(ICoreWindow& window, std::uint64_t off, std::size_t n);
    [[nodiscard]] Ex<void> compare_step_(ICoreWindow& window);
    [[nodiscard]] Ex<void> write_step_(ICoreWindow& window, const svc::Vfs& vfs);
    [[nodiscard]] Ex<void> clear_(ICoreWindow& window);

    std::uint32_t addr_;
    SavePath disk_path_{};
    FixedStr<32, StrFit::Reject> core_dir_{};
    bool path_ok_ = false;
    Phase phase_ = Phase::Open;
    std::uint64_t off_ = 0;
    std::uint32_t total_ = 0;
    std::uint32_t cleared_ = 0;
    bool wrote_ = false;
    bool matched_ = false;
    std::optional<Work> work_{};
};

extern const RamSaveJob::Kind kRamSaveKind;

[[nodiscard]] bool write_ram_save_order(
    std::span<std::byte> order,
    std::uint32_t load_addr,
    std::string_view disk_path,
    std::string_view core_dir
) noexcept;

}
