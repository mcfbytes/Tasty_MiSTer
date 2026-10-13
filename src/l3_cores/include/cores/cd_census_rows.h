// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/cd_service_rows.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/sector_kick_row.h"
#include "cores/service_tick_row.h"
#include "reactor/link_decoder.h"
#include "reactor/link_decoder_decl.h"

namespace mister::cores {

namespace detail {

template <const CoreProfile& kProfile>
class CommandEdgeRow final : public reactor::ILinkDecoder {
public:
    void service(reactor::CoreState& st) const override {
        if (ICdServiceRows* r = rows_for(st, kProfile)) r->service_command_edge();
    }
};

}  // namespace detail

template <const CoreProfile& kProfile>
inline constexpr detail::CommandEdgeRow<kProfile> kCdCommandEdge{};
template <const CoreProfile& kProfile>
inline constexpr detail::ServiceTickRow<kProfile> kCdServiceTick{};

}  // namespace mister::cores
