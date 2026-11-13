// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::cores {

class ICoreInitHost {
public:
    virtual ~ICoreInitHost() = default;

    virtual void reset_video_geometry() = 0;
    virtual void bind_video_prelude() = 0;
    virtual void publish_core_identity() = 0;

protected:
    ICoreInitHost() = default;
    ICoreInitHost(const ICoreInitHost&) = default;
    ICoreInitHost& operator=(const ICoreInitHost&) = default;
};

}
