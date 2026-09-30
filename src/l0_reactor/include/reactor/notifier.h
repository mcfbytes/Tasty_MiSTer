// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/error.h"
#include "infra/unique_fd.h"
#include "hal/cause_reg.h"
#include "reactor/cause_set.h"
#include "hal/register_window.h"
#include "os/uio_handle.h"
#include "infra/seat.h"

namespace mister::reactor {

namespace cause_word {
inline constexpr std::uint32_t kPayload = 0x0000'FFFFu;
inline constexpr std::uint32_t kBlk = 1u << 16;
inline constexpr std::uint32_t kFifo = 1u << 17;
inline constexpr std::uint32_t kRing = 1u << 18;
inline constexpr std::uint32_t kTick = 1u << 19;
inline constexpr std::uint32_t kClasses = kBlk | kFifo | kRing | kTick;
inline constexpr std::uint32_t kReserved = 0xFFF0'0000u;
}  // namespace cause_word

struct CauseDecode {
    Cause cause = Cause::None;
    bool refused = false;
    friend constexpr bool operator==(CauseDecode, CauseDecode) = default;
};

constexpr CauseDecode decode_cause_checked(std::uint32_t word) noexcept {
    if ((word & cause_word::kReserved) != 0u) return {Cause::None, true};
    const std::uint32_t classes = word & cause_word::kClasses;
    if (classes == 0u) {
        return {Cause::None, (word & cause_word::kPayload) != 0u};
    }
    if ((classes & (classes - 1u)) != 0u) return {Cause::None, true};
    if (classes == cause_word::kFifo) return {Cause::Fifo, false};
    if (classes == cause_word::kBlk) return {Cause::Blk, false};
    if (classes == cause_word::kRing) return {Cause::Ring, false};
    return {Cause::CoreTick, false};
}

constexpr Cause decode_cause(std::uint32_t word) noexcept {
    return decode_cause_checked(word).cause;
}

class Notifier {
    TASTY_SEAT_MEDIATOR(Any, RT);

public:
    static Ex<Notifier> doorbell_adopt(os::UioHandle uio,
                                       const hal::RegisterWindow<hal::CauseReg>* cause,
                                       Cause klass);

    static Ex<Notifier> ticker(std::chrono::nanoseconds period);

    static Ex<Notifier> frame(int eventfd);

    static Ex<Notifier> test(int eventfd, Cause injected);

    Notifier(Notifier&&) noexcept = default;
    Notifier& operator=(Notifier&&) noexcept = default;

    int fd() const noexcept { return kind_ == Kind::Doorbell ? uio_.fd() : fd_.get(); }

    CauseSet delivers() const noexcept { return CauseSet{delivers_}; }

    CauseDecode drain_checked();
    Cause drain() { return drain_checked().cause; }

private:
    enum class Kind : std::uint8_t { Doorbell, Ticker, Frame, Test };
    Notifier(Kind k, UniqueFd fd, const hal::RegisterWindow<hal::CauseReg>* cause, Cause delivers)
        : kind_(k), fd_(static_cast<UniqueFd&&>(fd)), cause_(cause), delivers_(delivers) {}
    Notifier(os::UioHandle uio, const hal::RegisterWindow<hal::CauseReg>* cause, Cause klass)
        : kind_(Kind::Doorbell), uio_(static_cast<os::UioHandle&&>(uio)), cause_(cause),
          delivers_(klass) {}

    Kind kind_;
    UniqueFd fd_;
    os::UioHandle uio_;
    const hal::RegisterWindow<hal::CauseReg>* cause_ = nullptr;
    Cause delivers_ = Cause::None;
};

}  // namespace mister::reactor
