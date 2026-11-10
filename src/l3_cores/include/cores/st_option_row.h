// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/option_row_view.h"
#include "cores/option_rows.h"
#include "cores/st_ctrl.h"
#include "cores/st_options.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"

namespace mister::cores {

class StOptionRow {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct AcsiFact {
        bool bound = false;
        std::string_view path{};
    };

    struct Facts {
        st_ctrl::CtrlWord system_ctrl{};
        std::uint32_t ext_ctrl = 0;
        std::array<std::string_view, 2> floppy{};
        infra::IndexedArray<AcsiFact, 2, AcsiTarget> acsi{};
        std::string_view tos{};
        std::string_view cart{};
    };

    struct Write {
        std::optional<st_ctrl::CtrlWord> system_ctrl{};
        std::optional<std::uint32_t> ext_ctrl{};
    };

    [[nodiscard]] static const StOptionRow& of(StOption o) noexcept;

    [[nodiscard]] OptionRowView cycle_view(
        const Facts& f,
        const OptionRow::CycleArm& r
    ) const noexcept;

    [[nodiscard]] OptionRowView file_view(const Facts& f, std::span<char> text) const noexcept;
    [[nodiscard]] Ex<Write> cycle_set(const Facts& f, proto::FieldValue choice) const;

    StOptionRow(const StOptionRow&) = delete;
    StOptionRow& operator=(const StOptionRow&) = delete;

protected:
    constexpr explicit StOptionRow(StOption id) noexcept : id_(id) {}

    ~StOptionRow() = default;

private:
    [[nodiscard]] virtual OptionRowView on_cycle_view(
        const Facts& f,
        const OptionRow::CycleArm& r
    ) const noexcept;
    [[nodiscard]] virtual OptionRowView on_file_view(
        const Facts& f,
        std::span<char> text
    ) const noexcept;
    [[nodiscard]] virtual Ex<Write> on_cycle_set(const Facts& f, proto::FieldValue choice) const;

    StOption id_;
};

class StOptionRowPlain final : public StOptionRow {
    TASTY_SEAT_RESIDENT(RT);

public:
    constexpr explicit StOptionRowPlain(StOption id) noexcept : StOptionRow(id) {}
};

class StOptionRowBlitter final : public StOptionRow {
    TASTY_SEAT_RESIDENT(RT);

public:
    constexpr StOptionRowBlitter() noexcept : StOptionRow(StOption::Blitter) {}

private:
    [[nodiscard]] OptionRowView on_cycle_view(
        const Facts& f,
        const OptionRow::CycleArm& r
    ) const noexcept override;
    [[nodiscard]] Ex<Write> on_cycle_set(const Facts& f, proto::FieldValue choice) const override;
};

class StOptionRowViking final : public StOptionRow {
    TASTY_SEAT_RESIDENT(RT);

public:
    constexpr StOptionRowViking() noexcept : StOptionRow(StOption::Viking) {}

private:
    [[nodiscard]] OptionRowView on_cycle_view(
        const Facts& f,
        const OptionRow::CycleArm& r
    ) const noexcept override;
    [[nodiscard]] Ex<Write> on_cycle_set(const Facts& f, proto::FieldValue choice) const override;
};

class StOptionRowAspect final : public StOptionRow {
    TASTY_SEAT_RESIDENT(RT);

public:
    constexpr StOptionRowAspect() noexcept : StOptionRow(StOption::Aspect) {}

private:
    [[nodiscard]] OptionRowView on_cycle_view(
        const Facts& f,
        const OptionRow::CycleArm& r
    ) const noexcept override;
    [[nodiscard]] Ex<Write> on_cycle_set(const Facts& f, proto::FieldValue choice) const override;
};

class StOptionRowVideoCrop final : public StOptionRow {
    TASTY_SEAT_RESIDENT(RT);

public:
    constexpr StOptionRowVideoCrop() noexcept : StOptionRow(StOption::VideoCrop) {}

private:
    [[nodiscard]] OptionRowView on_cycle_view(
        const Facts& f,
        const OptionRow::CycleArm& r
    ) const noexcept override;
    [[nodiscard]] Ex<Write> on_cycle_set(const Facts& f, proto::FieldValue choice) const override;
};

class StOptionRowFloppy final : public StOptionRow {
    TASTY_SEAT_RESIDENT(RT);

public:
    constexpr StOptionRowFloppy(StOption id, st_ctrl::StSlot drive) noexcept
        : StOptionRow(id),
          drive_(drive) {}

private:
    [[nodiscard]] OptionRowView on_file_view(
        const Facts& f,
        std::span<char> text
    ) const noexcept override;

    st_ctrl::StSlot drive_;
};

class StOptionRowHdd final : public StOptionRow {
    TASTY_SEAT_RESIDENT(RT);

public:
    constexpr StOptionRowHdd(StOption id, AcsiTarget target) noexcept
        : StOptionRow(id),
          target_(target) {}

private:
    [[nodiscard]] OptionRowView on_file_view(
        const Facts& f,
        std::span<char> text
    ) const noexcept override;

    AcsiTarget target_;
};

class StOptionRowTos final : public StOptionRow {
    TASTY_SEAT_RESIDENT(RT);

public:
    constexpr StOptionRowTos() noexcept : StOptionRow(StOption::Tos) {}

private:
    [[nodiscard]] OptionRowView on_file_view(
        const Facts& f,
        std::span<char> text
    ) const noexcept override;
};

class StOptionRowCartridge final : public StOptionRow {
    TASTY_SEAT_RESIDENT(RT);

public:
    constexpr StOptionRowCartridge() noexcept : StOptionRow(StOption::Cartridge) {}

private:
    [[nodiscard]] OptionRowView on_file_view(
        const Facts& f,
        std::span<char> text
    ) const noexcept override;
};

}
