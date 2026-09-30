// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::proto {
struct LinkOp;
}

namespace mister::app {

class LinkTxChannel;
struct LinkOpCtx;

class ILinkEncoder {
public:
    enum class Outcome : std::uint8_t { Dropped, Encoded, EncodedStatusChange };

    [[nodiscard]] virtual Outcome encode(const proto::LinkOp& op,
                                         const LinkOpCtx& ctx) noexcept = 0;

    ILinkEncoder(const ILinkEncoder&) = delete;
    ILinkEncoder& operator=(const ILinkEncoder&) = delete;

protected:
    constexpr ILinkEncoder() noexcept = default;
    ~ILinkEncoder() = default;
};

}  // namespace mister::app
