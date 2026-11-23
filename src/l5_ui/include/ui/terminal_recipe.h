// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "infra/error.h"
#include "infra/file_mode.h"
#include "infra/path_text.h"
#include "infra/prelude.h"
#include "os/path_buf.h"
#include "os/types.h"

namespace mister::ui {

inline constexpr os::VtNumber kTerminalVt{2};

inline constexpr std::string_view kTerminalScriptPath{"/tmp/script"};

inline constexpr infra::FileMode kTerminalScriptMode{0755};

class TerminalRecipe {
public:
    struct Paths {
        std::string tty;
        std::string script;
    };

    TerminalRecipe(std::vector<std::string> argv, Paths paths);

    [[nodiscard]] static TerminalRecipe stock();

    [[nodiscard]] std::span<const std::string> argv() const noexcept { return argv_; }

    [[nodiscard]] const infra::PathText& tty() const noexcept { return tty_; }

    [[nodiscard]] const infra::PathText& script_path() const noexcept { return script_path_; }

private:
    std::vector<std::string> argv_;
    infra::PathText tty_{};
    infra::PathText script_path_{};
};

inline TerminalRecipe::TerminalRecipe(std::vector<std::string> argv, Paths paths) {
    argv_ = std::move(argv);
    if (!tty_.assign(paths.tty) || !script_path_.assign(paths.script))
        infra::fatal(Error{Errc::bad_format, ERR_SITE(), 0}, "TerminalRecipe path");
}

inline TerminalRecipe TerminalRecipe::stock() {
    std::array<char, os::kVtTtyPathBuf> node{};
    const auto path = kTerminalVt.tty_path(std::span<char>(node));
    if (!path)
        infra::fatal(Error{Errc::bad_format, ERR_SITE(), 1}, "TerminalRecipe tty");
    constexpr std::string_view kDevPrefix{"/dev/"};
    if (!path->starts_with(kDevPrefix))
        infra::fatal(Error{Errc::bad_format, ERR_SITE(), 0}, "TerminalRecipe tty prefix");
    std::vector<std::string> argv{
        "/sbin/agetty",
        "-a",
        "root",
        "-l",
        std::string(kTerminalScriptPath),
        "--nohostname",
        "-L",
        std::string(path->substr(kDevPrefix.size())),
        "linux"
    };
    return TerminalRecipe{
        std::move(argv),
        Paths{.tty = std::string(*path), .script = std::string(kTerminalScriptPath)}
    };
}

}
