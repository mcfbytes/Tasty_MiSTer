// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "app/path_text.h"
#include "cores/boot_asset.h"
#include "cores/start_rule.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

struct StartFile {
    PathText path{};
    proto::WideIoIndex dest{};
    std::uint64_t offset = 0;
};

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
    };

    [[nodiscard]] static StartChain plan(const Facts& f, const svc::Vfs& vfs);
    [[nodiscard]] std::span<const StartFile> files() const noexcept {
        return {files_.data(), count_};
    }
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
    void add_(std::string_view path, proto::WideIoIndex dest, std::uint64_t offset = 0);
    [[nodiscard]] static std::optional<StartFile> file_(std::string_view path,
                                                        proto::WideIoIndex dest,
                                                        std::uint64_t offset);

    static constexpr std::size_t kBootImages = 5;
    std::array<StartFile, cores::kMaxStartAssets> files_{};
    std::size_t count_ = 0;
    std::array<StartFile, kBootImages> images_{};
    std::size_t image_count_ = 0;
    bool hold_ = false;
};

}  // namespace mister::app
