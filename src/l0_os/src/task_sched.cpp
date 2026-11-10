// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/task_sched.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include "infra/parse_int.h"

#include "proc_text.h"

namespace mister::os {
namespace {

constexpr std::uint32_t kStateField = 0;
constexpr std::uint32_t kRtPriorityField = 37;
constexpr std::uint32_t kPolicyField = 38;

[[nodiscard]] std::string_view next_token(std::string_view& rest) noexcept {
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\n'))
        rest.remove_prefix(1);
    if (rest.empty())
        return {};
    const std::string_view token = rest.substr(0, rest.find_first_of(" \n"));
    rest.remove_prefix(token.size());
    return token;
}

}

[[nodiscard]] Ex<TaskSched> read_task_sched(KernelTid tid) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/self/task/%s/stat", tid.text().c_str());
    std::array<char, 1024> buf{};
    const auto text = detail::read_proc_text(path, std::span(buf));
    if (!text)
        return std::unexpected(text.error());

    const std::size_t close = text->rfind(')');
    if (close == std::string_view::npos) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    std::string_view rest = text->substr(close + 1);

    TaskSched sched{};
    bool saw_priority = false;
    bool saw_policy = false;
    std::uint32_t token = 0;
    for (std::string_view word = next_token(rest); !word.empty(); word = next_token(rest)) {
        if (token == kStateField)
            sched.state = word.front();
        if (token == kRtPriorityField || token == kPolicyField) {
            const auto value = infra::parse_int<int>(word);
            if (!value || *value < 0) {
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), token});
            }
            if (token == kPolicyField) {
                sched.policy = sched_policy_of(*value);
                saw_policy = true;
            } else {
                sched.rt_priority = RtPriority{*value};
                saw_priority = true;
            }
        }
        ++token;
    }
    if (!saw_policy || !saw_priority) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), token});
    }

    std::snprintf(path, sizeof path, "/proc/self/task/%s/wchan", tid.text().c_str());
    std::array<char, 64> wbuf{};
    if (const auto w = detail::read_proc_text(path, std::span(wbuf)); w) {
        const std::string_view line = w->substr(0, w->find('\n'));
        if (!line.empty() && line != "0")
            (void)sched.wchan.assign(line);
    }
    return sched;
}

}
