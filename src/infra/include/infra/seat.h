// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/error.h"

namespace mister {

enum class SeatTag : std::uint8_t {
    Unbound = 0,
    RT,
    Frame,
    Input,
    Pcm,
    Prefetch,
    Diag,
    Ui,
    Io,
    Capture,
    Encode,
    RecWrite,
    Launcher,
};
inline constexpr std::size_t kSeatTagCount = 13;

inline constexpr const char* kSeatNames[kSeatTagCount] = {
    "UNBOUND", "T-RT", "T-FRAME",   "T-INPUT",  "T-PCM",      "T-PREFETCH", "T-DIAG",
    "T-UI",    "T-IO", "T-CAPTURE", "T-ENCODE", "T-RECWRITE", "T-LAUNCHER",
};

constexpr const char* seat_name(SeatTag t) noexcept {
    const auto i = static_cast<std::size_t>(t);
    return i < kSeatTagCount ? kSeatNames[i] : "?";
}

[[nodiscard]] SeatTag current_seat() noexcept;

SeatTag adopt_seat_tag(SeatTag t) noexcept;

class SeatScope {
public:
    explicit SeatScope(SeatTag t) noexcept : prev_(adopt_seat_tag(t)) {}
    ~SeatScope() { (void)adopt_seat_tag(prev_); }
    SeatScope(const SeatScope&) = delete;
    SeatScope& operator=(const SeatScope&) = delete;
    SeatScope(SeatScope&&) = delete;
    SeatScope& operator=(SeatScope&&) = delete;

private:
    SeatTag prev_;
};

inline constexpr bool kSeatChecksEnabled = (0 != 0);

namespace seat_detail {

[[noreturn]] void violation(SeatTag want, SeatTag got, std::uint16_t site, const char* what);

inline void check(SeatTag want, std::uint16_t site, const char* what) noexcept {
    if (const SeatTag got = current_seat(); got != want) {
        violation(want, got, site, what);
    }
}

}  // namespace seat_detail

template <SeatTag Want>
inline void seat_assert([[maybe_unused]] std::uint16_t site,
                        [[maybe_unused]] const char* what) noexcept {
    if constexpr (kSeatChecksEnabled && Want != SeatTag::Unbound) {
        seat_detail::check(Want, site, what);
    }
}

namespace seat_arg {
inline constexpr SeatTag Any = SeatTag::Unbound;
inline constexpr SeatTag RT = SeatTag::RT;
inline constexpr SeatTag Frame = SeatTag::Frame;
inline constexpr SeatTag Input = SeatTag::Input;
inline constexpr SeatTag Pcm = SeatTag::Pcm;
inline constexpr SeatTag Prefetch = SeatTag::Prefetch;
inline constexpr SeatTag Diag = SeatTag::Diag;
inline constexpr SeatTag Ui = SeatTag::Ui;
inline constexpr SeatTag Io = SeatTag::Io;
inline constexpr SeatTag Capture = SeatTag::Capture;
inline constexpr SeatTag Encode = SeatTag::Encode;
inline constexpr SeatTag RecWrite = SeatTag::RecWrite;
inline constexpr SeatTag Launcher = SeatTag::Launcher;
}  // namespace seat_arg

namespace seat_exempt {
inline constexpr int component = 0;
inline constexpr int const_shared = 1;
inline constexpr int boot = 2;
inline constexpr int main = 3;
}  // namespace seat_exempt

}  // namespace mister

#define TASTY_SEAT_RESIDENT(tag) \
    static constexpr ::mister::SeatTag kSeatDecl = ::mister::SeatTag::tag

#define TASTY_SEAT_MEDIATOR(a, b)                                              \
    static constexpr ::mister::SeatTag kSeatMediatorA = ::mister::seat_arg::a; \
    static constexpr ::mister::SeatTag kSeatMediatorB = ::mister::seat_arg::b

#define TASTY_SEAT_EXEMPT(kind) static constexpr int kSeatExempt = ::mister::seat_exempt::kind

#define TASTY_SEAT_OPEN() static constexpr bool kSeatOpen = true

#define TASTY_SEAT(tag) ((void)0)

#define TASTY_SEAT_BODY(Type) ((void)0)
#define TASTY_SEAT_BODY_MEDIATOR(Type, Half) ((void)0)
