// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "os/key_injector.h"

namespace mister::os {

class UinputKeyboard final : public IKeyInjector {
    TASTY_SEAT_RESIDENT(Input);

public:
    static constexpr const char* kName = "MiSTer virtual input";

    [[nodiscard]] Ex<void> open() noexcept override;
    void key(std::uint16_t code, bool down) noexcept override;

private:
    UniqueFd fd_{};
};

}  // namespace mister::os
