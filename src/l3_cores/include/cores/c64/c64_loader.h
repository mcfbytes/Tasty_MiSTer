// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "cores/c64/easyflash_image.h"
#include "cores/core_profile.h"
#include "cores/loader.h"
#include "cores/loader_context.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/status_word.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::cores::c64 {

class C64Loader final : public ILoader {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit C64Loader(const LoaderContext& ctx) noexcept
        : vfs_(ctx.vfs),
          core_dir_(ctx.profile.name) {}

    [[nodiscard]] Ex<LoadPlan> plan(const LoadAsk& ask, const proto::StatusWord& status) override;

    [[nodiscard]] Ex<std::unique_ptr<svc::IFile>> source(
        const LoadAsk& ask,
        std::unique_ptr<svc::IFile> raw
    ) override;
    [[nodiscard]] Ex<void> finish(infra::OptRef<ICoreWindow> window) override;

    [[nodiscard]] std::span<const std::uint8_t> facts() const noexcept override;

    [[nodiscard]] bool from_save() const noexcept { return from_save_; }

    [[nodiscard]] infra::Tally packets_skipped() const noexcept { return packets_skipped_; }

private:
    const svc::Vfs& vfs_;
    std::string_view core_dir_;
    CrtHeader header_{};
    bool framed_ = false;
    bool from_save_ = false;
    infra::Tally packets_skipped_{};
};

}
