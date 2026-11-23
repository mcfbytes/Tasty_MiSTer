// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "hal/types.h"
#include "infra/affine.h"
#include "infra/opt_ref.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"

namespace mister::proto {

inline constexpr std::size_t kMailboxActBytes = 1024;

struct MailboxRunCommand {
    hal::SpiWord opcode{};
    hal::SpiWord word{};
    hal::SpiWord tail{};
    friend constexpr bool operator==(const MailboxRunCommand&, const MailboxRunCommand&) = default;
};

enum class MailboxRunWords : std::uint8_t { One, Pair };

inline constexpr FioWordCount kMailboxRunCommandWords{2};

[[nodiscard]] constexpr FioWordCount run_command_words(MailboxRunWords words) noexcept {
    switch (words) {
        case MailboxRunWords::One:
            return kMailboxRunCommandWords;
        case MailboxRunWords::Pair:
            break;
    }
    return kMailboxRunCommandWords + FioWordCount{1};
}

static_assert(
    sizeof(std::array<MailboxRunCommand, user_io::kMaxPollFrameReplies.size()>) <= kMailboxActBytes,
    "the widest run, of pairs, fits one act's buffer"
);

struct MailboxRunGuard {
    std::uint8_t v = 0;
    static const MailboxRunGuard kNone;

    [[nodiscard]] constexpr bool is_none() const noexcept { return *this == kNone; }

    template <class Row>
    [[nodiscard]] constexpr infra::OptRef<const Row> among(
        std::span<const Row> guards
    ) const noexcept {
        if (is_none() || v >= guards.size())
            return {};
        return infra::OptRef<const Row>{guards[v]};
    }

    friend constexpr bool operator==(MailboxRunGuard, MailboxRunGuard) = default;
};

inline constexpr MailboxRunGuard MailboxRunGuard::kNone{0xFF};

class MailboxRun {
public:
    explicit constexpr MailboxRun(
        std::span<std::uint8_t, kMailboxActBytes> bytes,
        MailboxRunWords words = MailboxRunWords::One
    ) noexcept
        : rest_(bytes),
          words_(words) {}

    constexpr bool add(const MailboxRunCommand& command) noexcept {
        if (commands_ == user_io::kMaxPollFrameReplies)
            return false;
        put_(command.opcode);
        put_(command.word);
        if (words_ == MailboxRunWords::Pair)
            put_(command.tail);
        commands_ += ReplyCount{1};
        return true;
    }

    [[nodiscard]] constexpr ReplyCount commands() const noexcept { return commands_; }

    [[nodiscard]] constexpr MailboxRunWords words() const noexcept { return words_; }

    [[nodiscard]] constexpr bool empty() const noexcept { return commands_ == ReplyCount{}; }

private:
    constexpr void put_(hal::SpiWord word) noexcept {
        word.store_le(rest_.first<sizeof(hal::SpiWord)>());
        rest_ = rest_.subspan(sizeof(hal::SpiWord));
    }

    std::span<std::uint8_t> rest_;
    MailboxRunWords words_ = MailboxRunWords::One;
    ReplyCount commands_{};
};

class MailboxRunReader {
public:
    explicit constexpr MailboxRunReader(
        std::span<const std::uint8_t, kMailboxActBytes> bytes,
        MailboxRunWords words = MailboxRunWords::One
    ) noexcept
        : rest_(bytes),
          words_(words) {}

    [[nodiscard]] constexpr MailboxRunCommand next() noexcept {
        MailboxRunCommand command{};
        command.opcode = take_();
        command.word = take_();
        if (words_ == MailboxRunWords::Pair)
            command.tail = take_();
        return command;
    }

private:
    [[nodiscard]] constexpr hal::SpiWord take_() noexcept {
        const hal::SpiWord word = hal::SpiWord::load_le(rest_.first<sizeof(hal::SpiWord)>());
        rest_ = rest_.subspan(sizeof(hal::SpiWord));
        return word;
    }

    std::span<const std::uint8_t> rest_;
    MailboxRunWords words_ = MailboxRunWords::One;
};

}
