// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "cores/loader.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/status_word.h"
#include "svc/file.h"
#include "svc/gzip_inflate.h"

namespace mister::cores {

class UefLoader final : public ILoader {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] Ex<LoadPlan> plan(const LoadAsk& ask, const proto::StatusWord& status) override;
    [[nodiscard]] Ex<std::unique_ptr<svc::IFile>> source(
        const LoadAsk& ask,
        std::unique_ptr<svc::IFile> raw
    ) override;

    [[nodiscard]] Ex<void> finish(infra::OptRef<ICoreWindow>) override {
        TASTY_SEAT_BODY(UefLoader);
        return {};
    }

    [[nodiscard]] std::span<const std::uint8_t> facts() const noexcept override {
        TASTY_SEAT_BODY(UefLoader);
        return {};
    }

private:
    svc::GzipInflater inflater_{};
};

}
