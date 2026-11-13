// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>

#include "tasty_cli.h"
#include "tasty_cli_refusal.h"
#include "tasty_entry.h"
#include "tasty_exit.h"
#include "tasty_runtime.h"

namespace mister::fw {

class TastyCommand {
public:
    virtual ~TastyCommand() = default;

    [[nodiscard]] virtual std::optional<TastyRefusal> refuse(const TastyArgs&) const noexcept = 0;

    [[nodiscard]] virtual tasty::Exit run(
        const TastyArgs&,
        std::optional<ArgvArea> area,
        const TastyRuntime& rt
    ) = 0;
};

[[nodiscard]] std::unique_ptr<TastyCommand> make_tasty_command(TastyVerb v);

}
