// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <span>

namespace mister::app {

struct BootArgs {

    const char* core = "";

    std::optional<const char*> companion{};

    [[nodiscard]] static BootArgs from_argv(int argc, char* const argv[]) noexcept {
        return from_argv(std::span<const char* const>(argv, static_cast<std::size_t>(argc)));
    }

    [[nodiscard]] static BootArgs from_argv(std::span<const char* const> argv) noexcept {
        BootArgs args{};
        if (argv.size() > 1 && argv[1] != nullptr)
            args.core = argv[1];
        if (argv.size() > 2 && argv[2] != nullptr && argv[2][0] != '\0') {
            args.companion = argv[2];
        }
        return args;
    }
};

}
