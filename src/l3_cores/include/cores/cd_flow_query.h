// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/disc_engine.h"

namespace mister::cores {

class ICdFlowQuery {
public:
    virtual ~ICdFlowQuery() = default;

    virtual bool can_send_data(svc::TrackType type) = 0;

protected:
    ICdFlowQuery() = default;
    ICdFlowQuery(const ICdFlowQuery&) = default;
    ICdFlowQuery& operator=(const ICdFlowQuery&) = default;
};

}  // namespace mister::cores
