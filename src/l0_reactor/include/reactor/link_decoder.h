// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::reactor {

struct CoreState;

class ILinkDecoder {
public:
    virtual void service(CoreState& core) const = 0;
    virtual constexpr bool active() const { return true; }

    ILinkDecoder(const ILinkDecoder&) = delete;
    ILinkDecoder& operator=(const ILinkDecoder&) = delete;

protected:
    constexpr ILinkDecoder() noexcept = default;
    ~ILinkDecoder() = default;
};

}  // namespace mister::reactor
