// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/start_file.h"
#include "cores/boot_asset.h"
#include "cores/host_bit_row.h"
#include "cores/start_rule.h"
#include "infra/seat.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class StartChain {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Facts {
        cores::StartRule rule = cores::StartRule::None;
        std::span<const cores::BootAsset> declared{};
        std::string_view home{};
        std::string_view rbf{};
        bool manifest_load = false;
        bool generic_identity = false;
        bool index0_taken = false;
        bool remembered = false;
        bool ladder = false;
        bool slot0_held = false;
        std::span<const cores::HostBitRow> host_bits{};
        proto::StatusWord status{};

        std::span<const std::string_view> remembered_exts{};
    };

    struct Place {
        std::size_t v = 0;

        [[nodiscard]] constexpr std::span<const StartFile> burst(
            std::span<const StartFile> files
        ) const noexcept {
            const std::span<const StartFile> rest = files.subspan(std::min(v, files.size()));
            return rest.first(std::min(rest.size(), cores::kMaxStartAssets));
        }

        [[nodiscard]] constexpr Place past(std::span<const StartFile> files) const noexcept {
            return Place{v + burst(files).size()};
        }

        [[nodiscard]] constexpr bool ends(std::span<const StartFile> files) const noexcept {
            return v >= files.size();
        }
    };

    [[nodiscard]] static StartChain plan(const Facts& f, const svc::Vfs& vfs);

    [[nodiscard]] std::span<const StartFile> files() const noexcept { return files_; }

    [[nodiscard]] bool holds_reset() const noexcept { return hold_; }

    [[nodiscard]] std::span<const StartFile> boot_images() const noexcept {
        return {images_.data(), image_count_};
    }

    [[nodiscard]] static std::string core_home(std::string_view name, const svc::Vfs& vfs);

private:
    void plan_assets_(const Facts& f, const svc::Vfs& vfs);
    void plan_chain_(const Facts& f, const svc::Vfs& vfs);
    [[nodiscard]] bool plan_composite_(std::string_view rbf, const svc::Vfs& vfs);
    void plan_boot_images_(bool slot0_held, std::string_view home, const svc::Vfs& vfs);
    void add_(
        std::string_view path,
        proto::WideIoIndex dest,
        proto::FileOffset at = proto::kFileStart
    );

    [[nodiscard]] static bool sends_(const cores::BootAsset& row, const Facts& f) noexcept;
    [[nodiscard]] static std::optional<StartFile> file_(
        std::string_view path,
        proto::WideIoIndex dest,
        proto::FileOffset at
    );

    static constexpr std::size_t kBootImages = 5;

    static constexpr std::uint8_t kNumberedBootImages = 4;

    std::vector<StartFile> files_{};
    std::array<StartFile, kBootImages> images_{};
    std::size_t image_count_ = 0;
    bool hold_ = false;
};

}
