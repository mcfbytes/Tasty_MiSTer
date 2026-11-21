// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "hal/fpga_memory.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/save_state_decl.h"
#include "proto/save_state_name.h"
#include "proto/link/savestate_engine.h"
#include "proto/types.h"

namespace mister::proto::link {

class SavePrimer {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr FileSize kStepBytes{1024u * 1024u};

    struct Env {
        TASTY_SEAT_EXEMPT(component);
        SaveStateEngine::ISaveStateFiles& files;
        SaveStateEngine::ISlotMap& map;
    };

    [[nodiscard]] Ex<void> begin(
        const Env& env,
        const SaveTarget& target,
        const SaveStateDecl& ss,
        const hal::FpgaAperture& aperture
    );

    [[nodiscard]] Ex<bool> step(const Env& env);

    [[nodiscard]] bool priming() const noexcept { return slot_.has_value(); }

    void end() noexcept;

    [[nodiscard]] unsigned load_failures() const noexcept { return load_failures_; }

private:
    [[nodiscard]] bool load_step_(const Env& env, hal::FpgaMemory& mem);

    SaveStateDecl window_{};
    infra::IndexedArray<std::optional<hal::FpgaMemory>, SaveStateEngine::kSlots, StateSlot>
        regions_{};
    SaveStateName base_{};

    std::optional<StateSlot> slot_;
    bool loading_ = false;

    std::optional<StateSlot> file_slot_;
    std::size_t off_ = 0;
    unsigned load_failures_ = 0;
};

}
