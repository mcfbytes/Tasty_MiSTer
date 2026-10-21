// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <span>

#include "infra/seat.h"
#include "menu_mode.h"
#include "process_standup.h"
#include "tasty_entry.h"

extern char** environ;

int main(int argc, char* argv[]) {
    (void)mister::adopt_seat_tag(mister::SeatTag::Main);
    const std::span<const char* const> args{argv, static_cast<std::size_t>(argc)};
    if (const auto tasty = mister::fw::tasty_invocation(args))
        return mister::fw::run_tasty(*tasty, mister::fw::ArgvArea{argc, argv, environ});
    mister::fw::MenuMode mode{argc, argv};
    mister::fw::ProcessStandup standup{mode};
    return standup.run();
}
