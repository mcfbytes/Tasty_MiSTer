// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/dbus_auth.h"

#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "infra/byte_view.h"
#include "infra/decimal_text.h"
#include "os/types.h"

namespace mister::os {
namespace {

constexpr std::string_view kAuthPrefix{"\0AUTH EXTERNAL ", 15};
constexpr std::string_view kLineEnd{"\r\n", 2};
constexpr std::string_view kBeginLine{"BEGIN\r\n", 7};
constexpr std::string_view kOkPrefix{"OK"};
constexpr char kNewline = '\n';

}

DbusAuth::DbusAuth() noexcept {
    out_length_ = build_auth_line();
    state_ = State::AwaitOk;
}

WireBytes DbusAuth::build_auth_line() noexcept {
    const infra::DecimalText uid_text{::geteuid()};
    const std::string_view uid = uid_text.view();
    std::span<char> store{out_};
    std::memcpy(store.data(), kAuthPrefix.data(), kAuthPrefix.size());
    std::size_t at = kAuthPrefix.size();
    for (const char digit : uid) {

        const std::array<char, 2> pair = hex_pair(as_byte(digit));
        std::memcpy(store.data() + at, pair.data(), pair.size());
        at += pair.size();
    }
    std::memcpy(store.data() + at, kLineEnd.data(), kLineEnd.size());
    at += kLineEnd.size();
    return WireBytes{at};
}

void DbusAuth::build_begin() noexcept {
    std::memcpy(out_.data(), kBeginLine.data(), kBeginLine.size());
    out_at_ = BufferOffset::kStart;
    out_length_ = WireBytes{kBeginLine.size()};
    state_ = State::SendBegin;
}

std::span<const std::byte> DbusAuth::pending() const noexcept {
    const std::span<const char> text = out_at_.up_to(
        as_offset(out_length_),
        std::span<const char>{out_}
    );
    return infra::text_bytes(std::string_view{text.data(), text.size()});
}

void DbusAuth::wrote(WireBytes bytes) noexcept {
    out_at_ = BufferOffset::least(out_at_ + bytes, as_offset(out_length_));
    if (out_at_ == as_offset(out_length_) && state_ == State::SendBegin)
        state_ = State::Complete;
}

WireBytes DbusAuth::consume(std::span<const std::byte> bytes) noexcept {
    if (state_ != State::AwaitOk)
        return WireBytes::kNone;
    std::size_t taken = 0;
    for (const std::byte received : bytes) {
        const char c = as_wire(received).as_char();
        if (line_length_ + WireU8::kWidth >= WireBytes{kMaxLine}) {
            state_ = State::Rejected;
            return WireBytes{taken + 1};
        }
        text_at(std::span<char>{line_}, as_offset(line_length_)) = c;
        line_length_ = line_length_ + WireU8::kWidth;
        ++taken;
        if (c != kNewline)
            continue;

        const std::span<const char> text = line_length_.head_of(std::span<const char>{line_});
        const std::string_view line{text.data(), text.size()};
        if (line.starts_with(kOkPrefix)) {
            build_begin();
        } else {
            state_ = State::Rejected;
        }
        return WireBytes{taken};
    }
    return WireBytes{taken};
}

}
