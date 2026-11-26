// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/reg_field.h"
#include "hal/uio_frame_lane.h"

namespace mister::hal::ocram {

class RecordHeader;

class LaneArg {
    using Field = RegField<std::uint8_t>;

public:
    using Sel = UioFrameLane::Sel;

    class Tag {
    public:
        static constexpr std::uint8_t kCount = 16;

        constexpr Tag() noexcept = default;

        [[nodiscard]] static constexpr std::optional<Tag> of(std::uint8_t tag) noexcept {
            if (tag >= kCount)
                return std::nullopt;
            return Tag{Field::Value{tag}};
        }

        friend constexpr bool operator==(Tag, Tag) = default;

    private:
        friend class LaneArg;

        constexpr explicit Tag(Field::Value tag) noexcept : v(tag) {}

        Field::Value v{};
    };

    constexpr LaneArg() noexcept = default;

    [[nodiscard]] static constexpr LaneArg of(Sel sel, bool odd) noexcept {
        return LaneArg{kOdd.set(kSel.set(0, sel_field_(sel)), Field::Value{odd})};
    }

    [[nodiscard]] static constexpr LaneArg fpga(bool odd) noexcept { return of(Sel::Fpga, odd); }

    [[nodiscard]] constexpr LaneArg capturing(Tag tag) const noexcept {
        return LaneArg{kTag.set(kCapture.set(v, Field::Value{true}), tag.v)};
    }

    [[nodiscard]] constexpr Sel sel() const noexcept {
        switch (kSel.get(v)) {
            case kUioSel:
                return Sel::Uio;
            case kFpgaSel:
                return Sel::Fpga;
            case kOsdHdmiSel:
                return Sel::OsdHdmi;
            default:
                return Sel::OsdVga;
        }
    }

    [[nodiscard]] constexpr bool odd() const noexcept { return kOdd.test(v); }

    [[nodiscard]] constexpr std::optional<Tag> tag() const noexcept {
        if (!kCapture.test(v))
            return std::nullopt;
        return Tag{Field::Value{kTag.get(v)}};
    }

    friend constexpr bool operator==(LaneArg, LaneArg) = default;

private:
    friend class RecordHeader;
    static constexpr Field kSel{0, 0x3};
    static constexpr Field kCapture{2, 1};
    static constexpr Field kTag{3, 0xF};
    static constexpr Field kOdd{7, 1};
    static constexpr std::uint8_t kUioSel = 0;
    static constexpr std::uint8_t kFpgaSel = 1;
    static constexpr std::uint8_t kOsdHdmiSel = 2;
    static constexpr std::uint8_t kOsdVgaSel = 3;

    [[nodiscard]] static constexpr Field::Value sel_field_(Sel sel) noexcept {
        switch (sel) {
            case Sel::Uio:
                return Field::Value{kUioSel};
            case Sel::Fpga:
                return Field::Value{kFpgaSel};
            case Sel::OsdHdmi:
                return Field::Value{kOsdHdmiSel};
            case Sel::OsdVga:
                return Field::Value{kOsdVgaSel};
        }
        return Field::Value{kFpgaSel};
    }

    constexpr explicit LaneArg(std::uint8_t arg) noexcept : v(arg) {}

    [[nodiscard]] constexpr RegField<std::uint32_t>::Value field() const noexcept {
        return RegField<std::uint32_t>::Value{v};
    }

    std::uint8_t v = kSel.set(0, Field::Value{kFpgaSel});
};

static_assert(LaneArg::fpga(true) == LaneArg::of(LaneArg::Sel::Fpga, true));
static_assert(LaneArg::of(LaneArg::Sel::OsdVga, false).sel() == LaneArg::Sel::OsdVga);
static_assert(!LaneArg::fpga(false).tag().has_value(), "a plain frame carries no tag");

}
