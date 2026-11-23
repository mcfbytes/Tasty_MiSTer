// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "cores/atari8/atari8_machine.h"
#include "cores/atari8/cart_form.h"
#include "cores/atari8/cart_match.h"
#include "cores/load_ask.h"
#include "cores/load_plan.h"
#include "cores/loader.h"
#include "cores/loader_context.h"
#include "cores/loader_memo.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/status_word.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::cores::atari8 {

class Atari8Loader final : public ILoader {
    TASTY_SEAT_RESIDENT(Main);

public:
    Atari8Loader(const LoaderContext& ctx, Atari8Machine machine) noexcept
        : vfs_(ctx.vfs),
          memo_(ctx.memo),
          machine_(machine) {}

    [[nodiscard]] Ex<LoadPlan> plan(const LoadAsk& ask, const proto::StatusWord& status) override;

    [[nodiscard]] Ex<std::unique_ptr<svc::IFile>> source(
        const LoadAsk& ask,
        std::unique_ptr<svc::IFile> raw
    ) override;

    [[nodiscard]] Ex<void> finish(infra::OptRef<ICoreWindow> window) override;
    [[nodiscard]] std::span<const std::uint8_t> facts() const noexcept override;

    [[nodiscard]] const std::optional<CartMatch>& planned() const noexcept { return planned_; }

private:
    const svc::Vfs& vfs_;
    LoaderMemo& memo_;
    Atari8Machine machine_;
    std::optional<CartMatch> planned_{};

    std::optional<proto::FileSize> first_cart_{};
    CartForm form_ = CartForm::Raw;
};

}
