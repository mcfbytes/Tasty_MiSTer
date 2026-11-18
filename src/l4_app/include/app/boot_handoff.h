// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <concepts>
#include <type_traits>

#include "app/board_ops.h"
#include "app/boot_args.h"
#include "app/load_request.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::app {

template <class F>
concept CoreHoldVerb = std::invocable<F&>;

template <class F>
concept SaveSettleStep = std::invocable<F&> && std::same_as<std::invoke_result_t<F&>, bool>;

class BootHandoff {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit BootHandoff(infra::OptRef<ISleeper> sleeper) noexcept : sleeper_(sleeper) {}

    template <CoreHoldVerb Hold>
    [[nodiscard]] ExitReboot handoff_to_alternate_executable(
        const char* exe,
        const BootArgs& args,
        Hold&& hold
    ) {
        TASTY_SEAT_BODY(BootHandoff);
        sync_();
        hold();
        return exec_alternate_(exe, args);
    }

    template <CoreHoldVerb Hold>
    [[nodiscard]] Ex<void> re_exec(const LoadRequest& req, Hold&& hold) {
        TASTY_SEAT_BODY(BootHandoff);
        sync_();
        hold();
        return re_exec_(req);
    }

    template <SaveSettleStep Step>
    void settle_save_lane(Step&& step) {
        TASTY_SEAT_BODY(BootHandoff);
        for (std::chrono::milliseconds paced{}; paced < kSaveSettleBound; paced += kSettlePause) {
            if (!step())
                return;
            pause_();
        }
    }

private:
    static constexpr std::chrono::milliseconds kSettlePause{1};
    static constexpr std::chrono::milliseconds kSaveSettleBound{8192};

    static void sync_() noexcept;
    [[nodiscard]] static ExitReboot exec_alternate_(const char* exe, const BootArgs& args);
    [[nodiscard]] static Ex<void> re_exec_(const LoadRequest& req);
    void pause_() noexcept;

    infra::OptRef<ISleeper> sleeper_{};
};

}
