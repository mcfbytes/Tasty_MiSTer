// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_cheat_engine.h"

namespace mister::cores::n64 {

Ex<void> CheatEngine::apply(std::span<const std::uint8_t> table, CheatUnit unit) {
    TASTY_SEAT_BODY(CheatEngine);
    ++applies_;
    if (table.empty()) {
        count_ = 0;
        return {};
    }
    if (unit != CheatUnit{CheatCode::kBytes}) {
        count_ = 0;
        return std::unexpected(unit.refused(Errc::bad_format, ERR_SITE()));
    }
    std::size_t n = table.size() / CheatCode::kBytes;
    if (n > CheatCode::kMax) {
        dropped_records_ += static_cast<std::uint32_t>(n - CheatCode::kMax);
        n = CheatCode::kMax;
    }
    for (std::size_t i = 0; i < n; ++i) {
        codes_[i] = CheatCode::decode(
            table.subspan(i * CheatCode::kBytes).first<CheatCode::kBytes>()
        );
    }
    count_ = static_cast<std::uint32_t>(n);
    return {};
}

void CheatEngine::rearm_boot_codes() noexcept {
    TASTY_SEAT_BODY(CheatEngine);
    if (count_ == 0)
        return;
    for (std::uint32_t i = 0; i < count_; ++i)
        codes_[i].boot_code_done = false;
    due_ = clock_.now() + kResetGap;
}

CheatTick CheatEngine::tick(const CheatGates& gates) noexcept {
    TASTY_SEAT_BODY(CheatEngine);
    const os::RawMonotonicClock::time_point now = clock_.now();
    if (due_ && now < *due_)
        return CheatTick::Idle;

    if (!gates.loaded) {
        due_ = now + kNoGameGap;
        return CheatTick::Idle;
    }

    const std::chrono::nanoseconds gap = cadence_.next_gap(gates.system);

    const os::RawMonotonicClock::time_point base = (!due_ || *due_ + kMaxLag < now) ? now : *due_;
    due_ = base + gap;

    if (count_ == 0 || !gates.enabled)
        return CheatTick::Idle;
    if (!window_) {
        ++window_refusals_;
        return CheatTick::Idle;
    }

    ++passes_;
    for (std::uint32_t i = 0; i < count_; ++i) {
        switch (execute_(codes_[i], gates.gs_pressed)) {
            case CodeOutcome::Unfulfilled:

                while (i + 1 < count_ && codes_[i + 1].chained())
                    ++i;
                continue;
            case CodeOutcome::Done:
                continue;
            case CodeOutcome::Invalid:
                ++invalids_;
                return CheatTick::Invalid;
        }
    }
    return CheatTick::Ran;
}

CheatEngine::CodeOutcome CheatEngine::execute_(CheatCode& c, bool gs_pressed) noexcept {
    std::array<std::uint8_t, ByteMask::kBytes> mem{};
    const std::span<std::uint8_t> reach = c.mask.reach(mem);
    if (reach.empty())
        return CodeOutcome::Invalid;

    if (c.address >= window_->size() || c.address + reach.size() > window_->size())
        return CodeOutcome::Invalid;

    if (c.boot_code) {
        if (c.boot_code_done)
            return CodeOutcome::Done;
        c.boot_code_done = true;
    }
    if (c.gs_button && !gs_pressed)
        return CodeOutcome::Done;

    if (!c.cmp_type || *c.cmp_type != Compare::Always) {
        auto got = window_->read(c.address, std::as_writable_bytes(reach));
        if (!got) {
            ++wire_faults_;

            return CodeOutcome::Done;
        }
        const CodeOutcome r = compare_(reach, c);
        if (r != CodeOutcome::Done)
            return r;
    }
    (void)write_back_(c);
    return CodeOutcome::Done;
}

CheatEngine::CodeOutcome CheatEngine::compare_(
    std::span<const std::uint8_t> mem,
    const CheatCode& c
) noexcept {
    std::uint32_t old_masked = 0;
    std::uint32_t new_masked = 0;
    c.mask.for_each_masked(mem, c.replace, [&](std::uint8_t m, std::uint8_t r) {
        old_masked = (old_masked << 8) | m;
        new_masked = (new_masked << 8) | r;
    });
    if (!c.cmp_type)
        return CodeOutcome::Invalid;
    switch (*c.cmp_type) {
        case Compare::Equal:
            return new_masked == old_masked ? CodeOutcome::Done : CodeOutcome::Unfulfilled;
        case Compare::Greater:
            return new_masked > old_masked ? CodeOutcome::Done : CodeOutcome::Unfulfilled;
        case Compare::Less:
            return new_masked < old_masked ? CodeOutcome::Done : CodeOutcome::Unfulfilled;
        case Compare::GreaterEq:
            return new_masked >= old_masked ? CodeOutcome::Done : CodeOutcome::Unfulfilled;
        case Compare::LessEq:
            return new_masked <= old_masked ? CodeOutcome::Done : CodeOutcome::Unfulfilled;
        case Compare::NotEq:
            return new_masked != old_masked ? CodeOutcome::Done : CodeOutcome::Unfulfilled;
        case Compare::Always:
        case Compare::Empty:
            return CodeOutcome::Invalid;
    }

    return CodeOutcome::Invalid;
}

bool CheatEngine::write_back_(const CheatCode& c) noexcept {
    const auto poke = [this](std::size_t at, std::span<const std::uint8_t> b) {
        auto r = window_->write(at, std::as_bytes(b));
        if (!r) {
            ++wire_faults_;
            return false;
        }
        ++code_writes_;
        return true;
    };

    bool ok = true;
    c.mask.for_each_run([&](ByteRun run) {
        ok = poke(
                 c.address + run.at,
                 std::span<const std::uint8_t>(c.replace).subspan(run.at, run.len)
             )
            && ok;
    });
    return ok;
}

std::uint32_t CheatEngine::count() const noexcept {
    TASTY_SEAT_BODY(CheatEngine);
    return count_;
}

std::uint32_t CheatEngine::applies() const noexcept {
    TASTY_SEAT_BODY(CheatEngine);
    return applies_;
}

std::uint32_t CheatEngine::passes() const noexcept {
    TASTY_SEAT_BODY(CheatEngine);
    return passes_;
}

std::uint32_t CheatEngine::code_writes() const noexcept {
    TASTY_SEAT_BODY(CheatEngine);
    return code_writes_;
}

std::uint32_t CheatEngine::invalids() const noexcept {
    TASTY_SEAT_BODY(CheatEngine);
    return invalids_;
}

std::uint32_t CheatEngine::window_refusals() const noexcept {
    TASTY_SEAT_BODY(CheatEngine);
    return window_refusals_;
}

std::uint32_t CheatEngine::wire_faults() const noexcept {
    TASTY_SEAT_BODY(CheatEngine);
    return wire_faults_;
}

std::uint32_t CheatEngine::dropped_records() const noexcept {
    TASTY_SEAT_BODY(CheatEngine);
    return dropped_records_;
}

}
