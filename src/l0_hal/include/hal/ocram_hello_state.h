// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/ocram_nonce.h"

namespace mister::hal::ocram {

class HelloState {
public:
    enum class Kind : std::uint8_t { Unknown, Armed, Waiting, Session, Stopped };

    static constexpr std::uint32_t kHelloTag = 0x4F43'0000u;

    constexpr HelloState() = default;

    [[nodiscard]] static constexpr HelloState unknown() noexcept { return HelloState{}; }

    [[nodiscard]] static constexpr HelloState armed() noexcept {
        return HelloState{Kind::Armed, Nonce{}};
    }

    [[nodiscard]] static constexpr HelloState waiting() noexcept {
        return HelloState{Kind::Waiting, Nonce{}};
    }

    [[nodiscard]] static constexpr HelloState stopped() noexcept {
        return HelloState{Kind::Stopped, Nonce{}};
    }

    [[nodiscard]] static constexpr HelloState session(Nonce nonce) noexcept {
        return HelloState{Kind::Session, nonce};
    }

    [[nodiscard]] static constexpr HelloState of(std::uint32_t word) noexcept {
        if (word == 0u)
            return unknown();
        if (word == kWaiting)
            return waiting();
        if (word == kStopped)
            return stopped();
        if ((word & kTagMask) != kHelloTag)
            return unknown();
        if ((word & kNonceMask) == 0u)
            return armed();
        return HelloState{Kind::Session, Nonce::minted(word)};
    }

    [[nodiscard]] constexpr std::uint32_t word() const noexcept {
        switch (kind_) {
            case Kind::Unknown:
                return 0u;
            case Kind::Armed:
                return kHelloTag;
            case Kind::Waiting:
                return kWaiting;
            case Kind::Stopped:
                return kStopped;
            case Kind::Session:
                return kHelloTag | nonce_.word();
        }
        return 0u;
    }

    [[nodiscard]] constexpr Kind kind() const noexcept { return kind_; }

    [[nodiscard]] constexpr std::optional<Nonce> nonce() const noexcept {
        return kind_ == Kind::Session ? std::optional<Nonce>{nonce_} : std::nullopt;
    }

    [[nodiscard]] constexpr bool echoes(HelloState h) const noexcept {
        return kind_ == Kind::Session && h.kind_ == Kind::Session && word() == h.word();
    }

    friend constexpr bool operator==(HelloState, HelloState) = default;

private:
    constexpr HelloState(Kind k, Nonce n) noexcept : kind_(k), nonce_(n) {}

    static constexpr std::uint32_t kTagMask = 0xFFFF'0000u;
    static constexpr std::uint32_t kNonceMask = 0x0000'FFFFu;
    static constexpr std::uint32_t kWaiting = 0x5754'0000u;
    static constexpr std::uint32_t kStopped = 0x5154'0000u;

    Kind kind_ = Kind::Unknown;
    Nonce nonce_{};
};

static_assert(sizeof(HelloState) == 4);
static_assert(
    HelloState::of(HelloState::session(Nonce::minted(7u)).word())
        == HelloState::session(Nonce::minted(7u)),
    "the hello word round-trips through its one decode"
);

}
