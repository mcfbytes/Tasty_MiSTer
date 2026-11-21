// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "infra/seat.h"
#include "svc/connect/bt_prompt.h"
#include "svc/connect/prompt_gen.h"

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
    svc::connect::PromptGen gen{};

    [[nodiscard]] static BtPromptRows of(const svc::connect::BtPrompt& prompt);
};

}
