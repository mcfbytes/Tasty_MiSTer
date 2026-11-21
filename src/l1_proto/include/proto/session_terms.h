// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"

namespace mister::proto {

struct SessionTerms {
    bool bursts_status = false;
    bool sends_preamble = false;
    bool polls_block = false;
    bool polls_status = false;
    bool releases_reset = false;
    bool analog_capable = false;
    bool plays_mgl = true;
    bool takes_start_chain = false;

    [[nodiscard]] static constexpr SessionTerms of(CoreType type) noexcept {
        switch (type) {
            case CoreType::EightBit:
                return SessionTerms{
                    .bursts_status = true,
                    .sends_preamble = true,
                    .polls_block = true,
                    .polls_status = true,
                    .releases_reset = true,
                    .analog_capable = true,
                    .plays_mgl = true,
                    .takes_start_chain = true
                };
            case CoreType::SharpMz:
                return SessionTerms{.plays_mgl = false};
            case CoreType::Unknown:
                return SessionTerms{};
        }
        return SessionTerms{};
    }

    friend constexpr bool operator==(const SessionTerms&, const SessionTerms&) = default;
};

}
