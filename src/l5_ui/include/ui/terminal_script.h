// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace mister::ui {

class TerminalScript {
public:
    [[nodiscard]] static TerminalScript for_user_script(std::string_view path);

    [[nodiscard]] std::string_view text() const noexcept { return text_; }

private:
    explicit TerminalScript(std::string text) noexcept : text_(std::move(text)) {}

    std::string text_;
};

namespace detail {

inline std::string single_quoted(std::string_view s) {
    std::string out;
    out.push_back('\'');
    for (const char c : s) {
        if (c == '\'')
            out.append("'\\''");
        else
            out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

}

inline TerminalScript TerminalScript::for_user_script(std::string_view path) {
    std::string text;
    text.append("#!/bin/bash\n");
    text.append("export LC_ALL=en_US.UTF-8\n");
    text.append("export HOME=/root\n");
    text.append("cd \"$(dirname ");
    text.append(detail::single_quoted(path));
    text.append(")\"\n");
    text.append(detail::single_quoted(path));
    text.append("\necho \"Press any key to continue\"\n");
    return TerminalScript{std::move(text)};
}

}
