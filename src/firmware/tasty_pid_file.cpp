// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_pid_file.h"

#include <array>
#include <span>
#include <string>
#include <string_view>

#include "app/path_text.h"
#include "infra/parse_int.h"
#include "os/small_file.h"
#include "tasty_say.h"

namespace mister::fw {

std::optional<os::ProcessId> TastyPidFile::pid() const noexcept {
    std::array<char, 32> buf{};
    const Ex<std::span<char>> got = os::read_small_file(paths_.pid.c_str(), buf);
    if (!got || got->empty())
        return std::nullopt;
    std::string_view s{got->data(), got->size()};
    if (const auto nl = s.find('\n'); nl != std::string_view::npos)
        s = s.substr(0, nl + 1);
    const auto raw = infra::parse_int<int>(infra::trim_line_end(s));
    if (!raw)
        return std::nullopt;
    return os::ProcessId::of(*raw);
}

std::optional<std::string> TastyPidFile::movie() const noexcept {
    std::array<char, app::kPathMax + 48> buf{};
    const Ex<std::span<char>> got = os::read_small_file(paths_.pid.c_str(), buf);
    if (!got || got->empty())
        return std::nullopt;
    std::string_view s{got->data(), got->size()};
    const auto nl = s.find('\n');
    if (nl == std::string_view::npos || nl + 1 >= s.size())
        return std::nullopt;
    s.remove_prefix(nl + 1);
    if (!s.empty() && s.back() == '\n')
        s.remove_suffix(1);
    if (s.empty())
        return std::nullopt;
    return std::string(s);
}

std::string TastyPidFile::busy_text() const noexcept {
    const auto pid = this->pid();
    const auto movie = this->movie();
    char buf[app::kPathMax + 64];
    if (pid && movie && !movie->empty()) {
        return std::string(tasty_render(
            buf,
            "tasty is already playing {} (pid {})",
            std::string_view{movie->c_str()},
            pid->printed()
        ));
    }
    if (pid) {
        return std::string(tasty_render(buf, "tasty is already playing (pid {})", pid->printed()));
    }
    return std::string(tasty_render(buf, "tasty is already playing"));
}

}
