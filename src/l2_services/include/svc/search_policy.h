// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

namespace mister::svc {

enum class SearchDir : std::uint8_t {
    CurrentRoot,
    SdRoot,
    UsbPrefixed,
    Network,
    Cifs,
    CoreDir,
    ConfigDir,
};

struct SearchPolicy {
    std::span<const SearchDir> order;
    bool case_insensitive = true;
};

namespace search {

inline constexpr SearchDir kPrefixed[] = {
    SearchDir::UsbPrefixed,
    SearchDir::Network,
    SearchDir::Cifs,
    SearchDir::CurrentRoot,
};
inline constexpr SearchDir kRootOnly[] = {SearchDir::CurrentRoot};
inline constexpr SearchDir kSdRootOnly[] = {SearchDir::SdRoot};
}  // namespace search

}  // namespace mister::svc
