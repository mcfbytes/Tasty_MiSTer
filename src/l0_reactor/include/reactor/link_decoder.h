// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::reactor {

struct CoreState;

class ILinkDecoder {
public:
    virtual void service(CoreState& core) const = 0;
    virtual constexpr bool active() const { return true; }

    virtual std::uint32_t osd_claims() const { return 0; }

    ILinkDecoder(const ILinkDecoder&) = delete;
    ILinkDecoder& operator=(const ILinkDecoder&) = delete;

protected:
    constexpr ILinkDecoder() noexcept = default;
    ~ILinkDecoder() = default;
};

}  // namespace mister::reactor
