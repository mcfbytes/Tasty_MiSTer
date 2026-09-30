// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_init.h"

namespace mister::app {

Ex<void> init_reset_video_geometry(CoreInitContext& ctx) {
    ctx.host.reset_video_geometry();
    return {};
}

Ex<void> init_arm_video_prelude(CoreInitContext& ctx) {
    ctx.host.arm_video_prelude();
    return {};
}

Ex<void> init_flush_status(CoreInitContext& ctx) { return ctx.session.flush_status(); }

Ex<void> init_publish_identity(CoreInitContext& ctx) {
    ctx.host.publish_core_identity();
    return {};
}

namespace {

Ex<void> walk(CoreInitContext& ctx, std::span<const CoreInitStep> rows) {
    const CoreTypeMask dialect = cores::dialect_bit(ctx.session.type());
    for (const CoreInitStep& step : rows) {
        if (step.fn == nullptr) continue;
        if (!cores::applies(step.applies_to, dialect)) continue;
        Ex<void> r = step.fn(ctx);
        if (!r && step.on_error == OnError::Fail) return r;
        (void)r;
    }
    return {};
}

}  // namespace

Ex<void> run_core_init(CoreInitContext& ctx, std::span<const CoreInitStep> additions) {
    if (auto r = walk(ctx, kCoreInitBase); !r) return r;
    return walk(ctx, additions);
}

}  // namespace mister::app
