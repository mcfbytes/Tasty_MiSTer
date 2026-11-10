// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/vm_status.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/parse_int.h"

#include "proc_text.h"

namespace mister::os {
namespace {

constexpr std::string_view kVmRssKey = "VmRSS:";
constexpr std::uint64_t kBytesPerKiB = 1024;

}

[[nodiscard]] Ex<infra::ByteCount> read_vm_rss_bytes() noexcept {
    std::array<char, 4096> buf{};
    const auto text = detail::read_proc_text("/proc/self/status", std::span(buf));
    if (!text)
        return std::unexpected(text.error());
    const std::size_t at = text->find(kVmRssKey);
    if (at == std::string_view::npos) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    std::string_view rest = text->substr(at + kVmRssKey.size());
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
        rest.remove_prefix(1);
    const auto kib = infra::parse_int<std::uint64_t>(rest.substr(0, rest.find_first_of(" \t\r\n")));
    if (!kib)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    return infra::ByteCount::of(*kib * kBytesPerKiB);
}

}
