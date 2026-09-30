// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/stage_plan.h"
#include "infra/error.h"
#include "proto/image_sink.h"
#include "proto/link_op.h"

namespace mister::cores {

enum class MountState : std::uint8_t { Done, Pending, Failed };

inline constexpr std::int64_t kDiscMountBoundNs = 5'000'000'000;

class IStagingCore {
public:
    virtual ~IStagingCore() = default;

    [[nodiscard]] virtual MountState mount_disc(std::string_view path) = 0;
    virtual void set_region(std::uint8_t region) = 0;
    virtual Ex<void> deferred_reset() = 0;

    virtual proto::IImageSink& image_sink() noexcept = 0;

    virtual StagePlan stage_plan() noexcept { return {}; }

    [[nodiscard]] virtual Ex<void> stage_disc_payload(const proto::LinkOp::StageDiscPayload&) {
        return {};
    }

protected:
    IStagingCore() = default;
    IStagingCore(const IStagingCore&) = default;
    IStagingCore& operator=(const IStagingCore&) = default;
};

}  // namespace mister::cores
