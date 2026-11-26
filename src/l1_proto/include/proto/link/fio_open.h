// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>
#include <type_traits>

#include "infra/fixed_str.h"
#include "proto/aux_value.h"
#include "proto/session_params.h"
#include "proto/types.h"

namespace mister::proto::link {

struct FioOpen {
    WideIoIndex index{};
    infra::FixedStr<8, infra::StrFit::Clip> ext{};
    AuxValue aux{};

    [[nodiscard]] static FioOpen of(WideIoIndex index, const SessionParams& params) noexcept {
        FioOpen open{.index = index, .aux = params.aux};
        (void)open.ext.assign(params.ext);
        return open;
    }

    [[nodiscard]] SessionParams params() const noexcept {
        return SessionParams{.aux = aux, .ext = ext.view()};
    }
};

static_assert(std::is_trivially_copyable_v<FioOpen>, "a level carries it whole");

}
