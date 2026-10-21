// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "hal/fpga_memory.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/conf_str.h"
#include "proto/savestate_engine.h"

namespace mister::proto {

class SavePrimer {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kStepBytes = 1024u * 1024u;

    struct Env {
        TASTY_SEAT_EXEMPT(component);
        SaveStateEngine::ISaveStateFiles& files;
        SaveStateEngine::ISlotMap& map;
    };

    [[nodiscard]] Ex<void> begin(
        const Env& env,
        const SaveStateEngine::SaveTarget& target,
        const SaveStateDecl& ss
    );

    [[nodiscard]] Ex<bool> step(const Env& env);

    [[nodiscard]] bool priming() const noexcept { return slot_ < SaveStateEngine::kSlots; }

    void end() noexcept;

    [[nodiscard]] unsigned load_failures() const noexcept { return load_failures_; }

private:
    [[nodiscard]] bool load_step_(const Env& env, hal::FpgaMemory& mem);

    SaveStateDecl window_{};
    std::optional<hal::FpgaMemory> regions_[SaveStateEngine::kSlots];
    std::string subdir_;
    std::string stem_;

    unsigned slot_ = SaveStateEngine::kSlots;
    bool loading_ = false;
    unsigned digit_ = 0;
    std::size_t off_ = 0;
    unsigned load_failures_ = 0;
};

}
