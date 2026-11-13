// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "infra/seat.h"
#include "svc/bt_prompt.h"
#include "svc/prompt_gen.h"

namespace mister::ui {

struct BtPromptRows {
    TASTY_SEAT_RESIDENT(Ui);

    enum class Ask : std::uint8_t {
        None,
        YesNo,
        AcceptReject,
        EnterPasskey,
        EnterPin,
    };

    std::string headline{};
    std::string value{};
    Ask ask = Ask::None;
    svc::PromptGen gen{};

    [[nodiscard]] static BtPromptRows of(const svc::BtPrompt& prompt);
};

}
