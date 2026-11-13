// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/path_text.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "tasty_cli.h"
#include "tasty_cli_refusal.h"
#include "tasty_files.h"

namespace mister::svc {
class Vfs;
}

namespace mister::fw {

class TastyPreflight {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit TastyPreflight(const svc::Vfs& vfs) noexcept : vfs_(vfs) {}

    [[nodiscard]] Ex<app::PathText> play_rom(const TastyArgs& a) const;

    [[nodiscard]] Ex<void> rom_readable(TastyFiles files) const;

private:
    const svc::Vfs& vfs_;
};

}
