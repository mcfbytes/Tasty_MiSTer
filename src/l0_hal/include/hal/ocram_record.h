// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "hal/ocram_info_arg.h"
#include "hal/ocram_lane_arg.h"
#include "hal/ocram_row_arg.h"
#include "hal/ocram_rumble_arg.h"
#include "hal/ocram_slot_arg.h"
#include "hal/ocram_slot_binds.h"
#include "hal/ocram_types.h"
#include "hal/ocram_upload_arg.h"
#include "hal/reg_field.h"
#include "hal/types.h"

namespace mister::hal::ocram {

template <class Owner>
class RingReader;

class RecordHeader {
public:
    enum class Kind : std::uint8_t {
        Row = 0x01,
        Frame = 0x10,
        Status = 0x20,
        Request = 0x21,
        Reply = 0x22,
        Write = 0x26,
        Bind = 0x27,
        Info = 0x28,
        Upload = 0x29,
        Rumble = 0x2A,
        LFrame = 0x30
    };

    static constexpr Words kRowWords{64};

    static constexpr Words kLbaWords{1};

    static constexpr Words kStatusWords{4};
    static constexpr Words kRumbleWords{1};

    [[nodiscard]] static constexpr RecordHeader row(RowArg arg) noexcept {
        return RecordHeader{Kind::Row, kArgField.set(0u, arg.field()), kRowWords};
    }

    [[nodiscard]] static constexpr RecordHeader frame(SlotArg arg, Words frame) noexcept {
        return RecordHeader{Kind::Frame, kArgField.set(0u, arg.field()), kLbaWords + frame};
    }

    [[nodiscard]] static constexpr RecordHeader request(SlotArg arg) noexcept {
        return RecordHeader{Kind::Request, kArgField.set(0u, arg.field()), kLbaWords};
    }

    [[nodiscard]] static constexpr RecordHeader lframe(LaneArg arg, Words words) noexcept {
        return RecordHeader{Kind::LFrame, kArgField.set(0u, arg.field()), words};
    }

    [[nodiscard]] static constexpr RecordHeader lframe_for(
        std::span<const LinkWord> link
    ) noexcept {
        return lframe_for(LaneArg::Sel::Fpga, link);
    }

    [[nodiscard]] static constexpr RecordHeader lframe_for(
        LaneArg::Sel sel,
        std::span<const LinkWord> link
    ) noexcept {
        return lframe(
            LaneArg::of(sel, link.size() % Words::kWordHalves != 0u),
            Words::carrying_halves(link.size())
        );
    }

    static constexpr void pack_lframe(
        std::span<const LinkWord> link,
        std::span<std::uint32_t> out
    ) noexcept {
        for (std::size_t i = 0; i < out.size(); ++i) {
            const std::size_t at = i * Words::kWordHalves;
            const LinkWord low = at < link.size() ? link[at] : LinkWord{};
            const LinkWord high = at + 1u < link.size() ? link[at + 1u] : LinkWord{};
            out[i] = LinkWord::joined(low, high);
        }
    }

    [[nodiscard]] static constexpr RecordHeader write(SlotArg arg, Words block) noexcept {
        return RecordHeader{Kind::Write, kArgField.set(0u, arg.field()), kLbaWords + block};
    }

    [[nodiscard]] static constexpr RecordHeader bind_record(SlotBinds binds) noexcept {
        return RecordHeader{Kind::Bind, kArgField.set(0u, binds.byte()), Words{}};
    }

    [[nodiscard]] static constexpr RecordHeader status() noexcept {
        return RecordHeader{Kind::Status, 0u, kStatusWords};
    }

    [[nodiscard]] static constexpr RecordHeader info(InfoArg arg) noexcept {
        return RecordHeader{Kind::Info, kArgField.set(0u, arg.field()), Words{}};
    }

    [[nodiscard]] static constexpr RecordHeader upload(UploadArg arg) noexcept {
        return RecordHeader{Kind::Upload, kArgField.set(0u, arg.field()), Words{}};
    }

    [[nodiscard]] static constexpr RecordHeader rumble(RumbleArg arg) noexcept {
        return RecordHeader{Kind::Rumble, kArgField.set(0u, arg.field()), kRumbleWords};
    }

    [[nodiscard]] constexpr Kind kind() const noexcept { return kind_; }

    [[nodiscard]] constexpr Words len() const noexcept { return len_; }

    [[nodiscard]] constexpr Words extent() const noexcept { return Words{1} + len_; }

    [[nodiscard]] constexpr SlotArg slot_arg() const noexcept {
        return SlotArg::of_field(kArgField.get(arg_));
    }

    [[nodiscard]] constexpr SlotBinds slot_binds() const noexcept {
        return SlotBinds{kArgField.get(arg_)};
    }

    [[nodiscard]] constexpr std::optional<InfoArg> info_arg() const noexcept {
        if (kind_ != Kind::Info)
            return std::nullopt;
        return InfoArg::of_field(kArgField.get(arg_));
    }

    [[nodiscard]] constexpr std::optional<UploadArg> upload_arg() const noexcept {
        if (kind_ != Kind::Upload)
            return std::nullopt;
        return UploadArg::of_field(kArgField.get(arg_));
    }

    [[nodiscard]] constexpr std::optional<RumbleArg> rumble_arg() const noexcept {
        if (kind_ != Kind::Rumble)
            return std::nullopt;
        return RumbleArg::of_field(kArgField.get(arg_));
    }

    [[nodiscard]] constexpr std::uint32_t word() const noexcept {
        return (static_cast<std::uint32_t>(kind_) << kKindShift) | arg_ | len_.word();
    }

    friend constexpr bool operator==(RecordHeader, RecordHeader) = default;

private:
    template <class Owner>
    friend class RingReader;

    static constexpr unsigned kKindShift = 24;
    static constexpr RegField<std::uint32_t> kArgField{16, 0xFF};
    static constexpr std::uint32_t kLenMask = 0xFFFFu;
    static constexpr Kind kKinds[] = {
        Kind::Row,
        Kind::Frame,
        Kind::Status,
        Kind::Request,
        Kind::Reply,
        Kind::Write,
        Kind::Bind,
        Kind::Info,
        Kind::Upload,
        Kind::Rumble
    };

    [[nodiscard]] static constexpr Words span_of(std::uint32_t word) noexcept {
        return Words{static_cast<std::uint16_t>(word & kLenMask)};
    }

    [[nodiscard]] static constexpr std::optional<RecordHeader> parse(std::uint32_t word) noexcept {
        const std::uint32_t kind = word >> kKindShift;
        for (const Kind k : kKinds) {
            if (static_cast<std::uint32_t>(k) == kind) {
                if (k == Kind::Rumble && !RumbleArg::of_field(kArgField.get(word)))
                    break;
                return RecordHeader{k, word & kArgField.word_mask(), span_of(word)};
            }
        }
        return std::nullopt;
    }

    constexpr RecordHeader(Kind kind, std::uint32_t arg_bits, Words len) noexcept
        : kind_(kind),
          arg_(arg_bits),
          len_(len) {}

    Kind kind_ = Kind::Row;
    std::uint32_t arg_ = 0;
    Words len_{};
};

static_assert(
    RecordHeader::row(RowArg{}).word() == 0x0100'0040u,
    "ROW's header is {kind 1, arg 0, 64 words}"
);
static_assert(
    RecordHeader::frame(SlotArg::primary(), Words{588}).word() == 0x1000'024Du,
    "a PSX FRAME's header is {kind 0x10, arg 0, 589 words}"
);
static_assert(
    RecordHeader::request(SlotArg::primary()).word() == 0x2100'0001u,
    "REQUEST's header is {kind 0x21, arg 0, 1 word}"
);
static_assert(
    RecordHeader::lframe(LaneArg::fpga(true), Words{2}).word() == 0x3081'0002u,
    "an odd LFRAME's header is {kind 0x30, arg {odd, sel 1}, 2 words}"
);
static_assert(
    RecordHeader::lframe(LaneArg::of(LaneArg::Sel::OsdVga, false), Words{1}).word() == 0x3003'0001u,
    "an even OSD-on-VGA LFRAME's header is {kind 0x30, arg {sel 3}, 1 word}"
);
static_assert(
    RecordHeader::write(SlotArg::of(MailboxSlot::Mount0), Words{256}).word() == 0x2601'0101u,
    "a 1 KB mount slot WRITE's header is {kind 0x26, k 1, 257 words}"
);
static_assert(
    RecordHeader::bind_record(SlotBinds{}.with(MailboxSlot::Mount1)).word() == 0x2704'0000u,
    "BIND's header is {kind 0x27, C_BIND[7:0], 0 words}"
);
static_assert(
    RecordHeader::status().word() == 0x2000'0004u,
    "STATUS's header is {kind 0x20, arg 0, 4 words}"
);
static_assert(
    RecordHeader::info(InfoArg::of(0x5A)).word() == 0x285A'0000u,
    "INFO's header is {kind 0x28, the info byte, 0 words}"
);
static_assert(
    RecordHeader::upload(UploadArg::of(3)).word() == 0x2903'0000u,
    "UPLOAD's header is {kind 0x29, the upload index, 0 words}"
);
static_assert(
    RecordHeader::rumble(RumbleArg::of(5).value()).word() == 0x2A05'0001u,
    "RUMBLE's header is {kind 0x2A, pad, 1 word}"
);

}
