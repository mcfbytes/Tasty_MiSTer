// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::svc {

struct VideoSample;

class IFilterResend {
public:
    virtual ~IFilterResend() = default;

    virtual bool plan_filter_send(const VideoSample& s, bool resend) = 0;

protected:
    IFilterResend() = default;
    IFilterResend(const IFilterResend&) = default;
    IFilterResend& operator=(const IFilterResend&) = default;
};

}  // namespace mister::svc
