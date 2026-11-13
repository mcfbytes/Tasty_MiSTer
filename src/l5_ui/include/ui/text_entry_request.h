// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/text_entry_kind.h"

namespace mister::ui {

class ITextEntryHost;

struct TextEntryRequest {
    TASTY_SEAT_RESIDENT(Ui);

    TextEntryKind kind = TextEntryKind::Passphrase;
    std::string_view title{};
    infra::OptRef<ITextEntryHost> host{};
};

}
