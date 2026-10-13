// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/cd_service_rows.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "reactor/link_decoder.h"

namespace mister::cores {

namespace detail {

[[nodiscard]] inline ICdServiceRows* rows_for(reactor::CoreState& st,
                                              const CoreProfile& p) noexcept {
    Core* c = bound_core(st);
    if (c == nullptr || &c->profile() != &p) return nullptr;
    return c->cd_service_rows();
}

template <const CoreProfile& kProfile>
class ServiceTickRow final : public reactor::ILinkDecoder {
public:
    void service(reactor::CoreState& st) const override {
        if (ICdServiceRows* r = rows_for(st, kProfile)) r->service_tick();
    }
};

}  // namespace detail

}  // namespace mister::cores
