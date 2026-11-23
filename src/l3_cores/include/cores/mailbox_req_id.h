// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

class MailboxReqId {
public:
    static constexpr std::uint16_t kNone = 0xFFFF;

    constexpr MailboxReqId() noexcept = default;

    explicit constexpr MailboxReqId(std::uint16_t wire) noexcept : wire_(wire) {}

    [[nodiscard]] constexpr MailboxReqId next() const noexcept {
        return MailboxReqId{static_cast<std::uint16_t>((wire_ + 1u) % kNone)};
    }

    friend bool operator==(const MailboxReqId&, const MailboxReqId&) = default;

private:
    std::uint16_t wire_ = kNone;
};

}
