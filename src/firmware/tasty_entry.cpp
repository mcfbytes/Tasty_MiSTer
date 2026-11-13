// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_entry.h"

#include <cstddef>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>

#include "tasty_cli.h"
#include "tasty_cli_refusal.h"
#include "tasty_command.h"
#include "tasty_exit.h"
#include "tasty_record.h"
#include "tasty_runtime.h"

namespace {

[[nodiscard]] mister::fw::tasty::Exit dispatch_tasty(
    std::span<const char* const> argv,
    std::optional<mister::fw::ArgvArea> area,
    const mister::fw::TastyRuntime& rt
) {
    rt.owner.adopt();
    if (argv.size() < 2) {
        mister::fw::tasty_print_usage();
        return mister::fw::tasty::Exit::Code::Refused;
    }
    auto parsed = mister::fw::parse_tasty_args(argv);
    if (!parsed) {
        std::fprintf(stderr, "tasty: %s\n", mister::fw::TastyRefusal::sentence_for(parsed.error()));
        return mister::fw::tasty::Exit::Code::Refused;
    }
    auto resolved = mister::fw::tasty_resolve_args(*parsed);
    if (!resolved) {
        std::fprintf(stderr, "tasty: cannot resolve paths\n");
        return mister::fw::tasty::Exit::Code::Refused;
    }
    const mister::fw::TastyArgs& a = *resolved;
    return mister::fw::make_tasty_command(a.verb)->run(a, area, rt);
}

}

std::optional<std::span<const char* const>> mister::fw::tasty_invocation(
    std::span<const char* const> argv
) noexcept {
    if (argv.empty() || argv[0] == nullptr)
        return std::nullopt;
    const std::string_view prog{argv[0]};
    const std::size_t slash = prog.rfind('/');
    const std::string_view base = slash == std::string_view::npos ? prog : prog.substr(slash + 1);
    if (base.find("tasty") != std::string_view::npos)
        return argv;
    if (argv.size() >= 2 && argv[1] != nullptr && std::string_view{argv[1]} == "tasty")
        return argv.subspan(1);
    return std::nullopt;
}

int mister::fw::run_tasty(
    std::span<const char* const> argv,
    std::optional<ArgvArea> area,
    const TastyRuntime& rt
) {

    return dispatch_tasty(argv, area, rt).status();
}
