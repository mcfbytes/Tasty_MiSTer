// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_cheat_engine.h"

namespace mister::cores::n64 {

namespace {

constexpr std::int64_t kMsNs = 1'000'000;

}

std::chrono::milliseconds NtscCadence::next_gap(bool pal) noexcept {
    std::chrono::milliseconds gap = kLongGap;
    switch (step_) {
        case Step::Unprimed:
            gap = kShortGap;
            step_ = Step::Wrapped;
            break;
        case Step::Wrapped:
            step_ = Step::FirstLong;
            break;
        case Step::FirstLong:
            step_ = Step::SecondLong;
            break;
        case Step::SecondLong:
            step_ = Step::Short;
            break;
        case Step::Short:
            gap = kShortGap;
            step_ = Step::FirstLong;
            break;
    }
    return pal ? kPalGap : gap;
}

Ex<void> CheatEngine::apply(std::span<const std::uint8_t> table, std::uint32_t unit) {
    TASTY_SEAT_BODY(CheatEngine);
    ++applies_;
    if (table.empty()) {
        count_ = 0;
        return {};
    }
    if (unit != CheatCode::kBytes) {
        count_ = 0;
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), unit});
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
    due_ns_ = clock_->now().count() + kResetGapMs * kMsNs;
}

CheatTick CheatEngine::tick(bool loaded, bool pal, bool enabled, bool gs_pressed) noexcept {
    TASTY_SEAT_BODY(CheatEngine);
    const std::int64_t now = clock_->now().count();
    if (due_ns_ != 0 && now < due_ns_)
        return CheatTick::Idle;

    if (!loaded) {
        due_ns_ = now + kNoGameGapMs * kMsNs;
        return CheatTick::Idle;
    }

    const std::int64_t gap_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    cadence_.next_gap(pal)
    )
                                    .count();

    const std::int64_t base = (due_ns_ == 0 || due_ns_ + kMaxLagMs * kMsNs < now) ? now : due_ns_;
    due_ns_ = base + gap_ns;

    if (count_ == 0 || !enabled)
        return CheatTick::Idle;
    if (window_ == nullptr) {
        ++window_refusals_;
        return CheatTick::Idle;
    }

    ++passes_;
    for (std::uint32_t i = 0; i < count_; ++i) {
        const int r = execute_(codes_[i], gs_pressed);
        if (r == 0) {

            while (i + 1 < count_ && codes_[i + 1].chained())
                ++i;
            continue;
        }
        if (r == 1)
            continue;
        ++invalids_;
        return CheatTick::Invalid;
    }
    return CheatTick::Ran;
}

int CheatEngine::execute_(CheatCode& c, bool gs_pressed) noexcept {
    std::array<std::uint8_t, ByteMask::kBytes> mem{};
    const std::span<std::uint8_t> reach = c.mask.reach(mem);
    if (reach.empty())
        return -1;

    if (c.address >= window_->size() || c.address + reach.size() > window_->size())
        return -1;

    if (c.boot_code) {
        if (c.boot_code_done)
            return 1;
        c.boot_code_done = true;
    }
    if (c.gs_button && !gs_pressed)
        return 1;

    if (!c.cmp_type || *c.cmp_type != Compare::Always) {
        auto got = window_->read(c.address, std::as_writable_bytes(reach));
        if (!got) {
            ++wire_faults_;
            return 1;
        }
        const int r = compare_(reach, c);
        if (r != 1)
            return r;
    }
    (void)write_back_(c);
    return 1;
}

int CheatEngine::compare_(std::span<const std::uint8_t> mem, const CheatCode& c) noexcept {
    std::uint32_t old_masked = 0;
    std::uint32_t new_masked = 0;
    c.mask.for_each_masked(mem, c.replace, [&](std::uint8_t m, std::uint8_t r) {
        old_masked = (old_masked << 8) | m;
        new_masked = (new_masked << 8) | r;
    });
    if (!c.cmp_type)
        return -1;
    switch (*c.cmp_type) {
        case Compare::Equal:
            return new_masked == old_masked ? 1 : 0;
        case Compare::Greater:
            return new_masked > old_masked ? 1 : 0;
        case Compare::Less:
            return new_masked < old_masked ? 1 : 0;
        case Compare::GreaterEq:
            return new_masked >= old_masked ? 1 : 0;
        case Compare::LessEq:
            return new_masked <= old_masked ? 1 : 0;
        case Compare::NotEq:
            return new_masked != old_masked ? 1 : 0;
        case Compare::Always:
        case Compare::Empty:
            return -1;
    }

    return -1;
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
