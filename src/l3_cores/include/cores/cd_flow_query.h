// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

class ICdFlowQuery {
public:
    virtual ~ICdFlowQuery() = default;

    virtual bool can_send_data(std::uint16_t selector) = 0;

protected:
    ICdFlowQuery() = default;
    ICdFlowQuery(const ICdFlowQuery&) = default;
    ICdFlowQuery& operator=(const ICdFlowQuery&) = default;
};

}
