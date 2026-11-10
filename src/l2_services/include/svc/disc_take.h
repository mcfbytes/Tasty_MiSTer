// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::svc {

class DiscTake {
public:
    enum class Answer : std::uint8_t { Bytes, NotResident, Failed };

    [[nodiscard]] static constexpr DiscTake bytes(proto::FileSize n) noexcept {
        return DiscTake{Answer::Bytes, n};
    }

    [[nodiscard]] static constexpr DiscTake not_resident() noexcept {
        return DiscTake{Answer::NotResident, proto::FileSize{}};
    }

    [[nodiscard]] static constexpr DiscTake failed() noexcept {
        return DiscTake{Answer::Failed, proto::FileSize{}};
    }

    [[nodiscard]] constexpr Answer answer() const noexcept { return answer_; }

    [[nodiscard]] constexpr bool is_bytes() const noexcept { return answer_ == Answer::Bytes; }

    [[nodiscard]] constexpr bool is_failed() const noexcept { return answer_ == Answer::Failed; }

    [[nodiscard]] constexpr proto::FileSize count() const noexcept { return count_; }

private:
    constexpr DiscTake(Answer a, proto::FileSize n) noexcept : answer_(a), count_(n) {}

    Answer answer_;
    proto::FileSize count_;
};

}
