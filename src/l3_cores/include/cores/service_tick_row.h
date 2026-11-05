// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/cd_service_rows.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "reactor/link_decoder.h"

namespace mister::cores {

namespace detail {

template <const CoreProfile& kProfile>
[[nodiscard]] inline infra::OptRef<ICdServiceRows> rows_for(reactor::CoreState& st) noexcept {
    Core* c = bound_core<kProfile>(st);
    if (c == nullptr)
        return {};
    return c->cd_service_rows();
}

template <const CoreProfile& kProfile>
class ServiceTickRow final : public reactor::ILinkDecoder {
public:
    void service(reactor::CoreState& st) const override {
        if (const auto r = rows_for<kProfile>(st))
            r->service_tick();
    }
};

}

}
