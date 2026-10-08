// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/sector_kick.h"
#include "reactor/core_state.h"
#include "reactor/link_decoder.h"

namespace mister::cores {

namespace detail {

template <const CoreProfile& kProfile>
class SectorKickRow final : public reactor::ILinkDecoder {
public:
    void service(reactor::CoreState& st) const override {
        Core* c = bound_core(st);
        if (c == nullptr || &c->profile() != &kProfile) return;
        if (ISectorKick* k = c->sector_kick()) k->service_kick();
    }
};

}  // namespace detail

template <const CoreProfile& kProfile>
inline constexpr detail::SectorKickRow<kProfile> kSectorKick{};

}  // namespace mister::cores
