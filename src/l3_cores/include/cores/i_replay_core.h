// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

#include "cores/movie_facts.h"
#include "cores/ram_image_recipe.h"
#include "cores/types.h"
#include "proto/bit_field.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::cores {

class IReplayCore {
public:
    static constexpr std::size_t kNeedsMax = 32;

    struct SettingNeed {
        proto::BitField field{};
        proto::ValueSet allowed{};
        proto::FieldValue preferred{};
        std::string_view name{};

        [[nodiscard]] static constexpr SettingNeed only(
            proto::BitField field,
            proto::FieldValue value,
            std::string_view name
        ) noexcept {
            return SettingNeed{
                .field = field,
                .allowed = value.bit(),
                .preferred = value,
                .name = name
            };
        }

        [[nodiscard]] proto::FieldValue value_in(const proto::StatusWord& live) const noexcept;
        [[nodiscard]] bool met_by(const proto::StatusWord& live) const noexcept;

        [[nodiscard]] bool settable() const noexcept { return allowed.holds(preferred); }
    };

    using NeedRows = std::array<SettingNeed, kNeedsMax>;

    struct NeedCount {
        std::size_t v = 0;

        [[nodiscard]] static constexpr NeedCount of(std::size_t n) noexcept { return NeedCount{n}; }

        [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

        [[nodiscard]] constexpr bool within(NeedCount limit) const noexcept { return v < limit.v; }

        constexpr void advance() noexcept { ++v; }

        [[nodiscard]] constexpr auto indices() const noexcept {
            return std::views::iota(std::size_t{0}, v);
        }

        [[nodiscard]] constexpr SettingNeed& slot_of(NeedRows& rows) const noexcept {
            return rows[v];
        }

        [[nodiscard]] constexpr std::span<const SettingNeed> prefix_of(
            const NeedRows& rows
        ) const noexcept {
            return {rows.data(), v};
        }
    };

    struct SettingNeeds {
        NeedRows rows{};
        NeedCount n{};

        void add(const SettingNeed& need) noexcept {
            if (n.within(NeedCount::of(rows.size()))) {
                n.slot_of(rows) = need;
                n.advance();
            }
        }

        [[nodiscard]] std::span<const SettingNeed> view() const& noexcept {
            return n.prefix_of(rows);
        }

        std::span<const SettingNeed> view() const&& = delete;
    };

    enum class RamFill : std::uint8_t { Zero, Ff, Random };

    struct Raster {
        std::chrono::nanoseconds period_ns{};
        std::chrono::nanoseconds line0_ns{};
    };

    enum class Parity : std::uint8_t { Any, Even, Odd, AfterSilence };

    enum class PowerOnEvent : std::uint8_t { LoadEnd, ResetPulse };

    struct PowerOn {
        CoreClocks clocks{};
        ClockHz clock_hz{};

        proto::BitField field{};
        proto::ValueSet when{};
        Parity p0 = Parity::Any;
        PowerOnEvent event = PowerOnEvent::LoadEnd;

        LateFrames late_frames{};
    };

    struct LeadRange {
        ReplayLead min{-8};
        ReplayLead max{64};

        struct Text {

            std::array<char, 24> v{};

            [[nodiscard]] const char* c_str() const noexcept { return v.data(); }
        };

        [[nodiscard]] Text text() const noexcept {
            Text out{};
            std::snprintf(out.v.data(), out.v.size(), "%d..%d", min.frames(), max.frames());
            return out;
        }

        [[nodiscard]] constexpr bool admits(ReplayLead lead) const noexcept {
            return lead >= min && lead <= max;
        }
    };

    struct Companion {
        std::array<std::string, 2> paths{};
        proto::FileSize size{};
    };

    virtual ~IReplayCore() = default;

    [[nodiscard]] virtual SettingNeeds setting_needs(const MovieFacts& f) const noexcept = 0;

    [[nodiscard]] virtual std::optional<SettingNeed> ram_fill_need(RamFill fill) const noexcept {
        (void)fill;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<SettingNeeds> setting_needs_for(
        const MovieFacts& f,
        std::optional<RamFill> fill
    ) const noexcept;

    [[nodiscard]] std::optional<std::string_view> check_settings(
        const proto::StatusWord& live,
        const MovieFacts& f
    ) const noexcept;

    [[nodiscard]] RamImageRecipe power_on_ram(const MovieFacts& f) const noexcept {
        return f.recorder_ram;
    }

    [[nodiscard]] virtual Raster raster(const MovieFacts& f) const noexcept = 0;
    [[nodiscard]] virtual PowerOn power_on(const MovieFacts& f) const noexcept = 0;

    [[nodiscard]] virtual LeadRange lead_range() const noexcept { return {}; }

    [[nodiscard]] std::optional<std::chrono::nanoseconds> power_on_ns(
        const proto::StatusWord& live,
        const MovieFacts& f
    ) const noexcept;

    [[nodiscard]] virtual std::string_view games_folder() const noexcept { return {}; }

    [[nodiscard]] virtual proto::FileSlotDigit rom_slot_digit() const noexcept = 0;

    [[nodiscard]] virtual bool rom_is_disc() const noexcept { return false; }

    [[nodiscard]] virtual std::string_view disc_images() const noexcept { return {}; }

    [[nodiscard]] virtual std::optional<Companion> companion(
        std::string_view rom,
        const MovieFacts& f
    ) const {
        (void)rom;
        (void)f;
        return std::nullopt;
    }

protected:
    IReplayCore() = default;
    IReplayCore(const IReplayCore&) = default;
    IReplayCore& operator=(const IReplayCore&) = default;
};

}
