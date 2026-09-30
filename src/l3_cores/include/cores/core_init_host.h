// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::cores {

class CoreInitHost {
public:
    virtual ~CoreInitHost() = default;

    virtual void reset_video_geometry() = 0;
    virtual void arm_video_prelude() = 0;
    virtual void publish_core_identity() = 0;

    [[nodiscard]] virtual bool declares_turbo() const noexcept = 0;
};

}  // namespace mister::cores
