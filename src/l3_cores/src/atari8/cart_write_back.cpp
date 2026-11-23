// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/cart_write_back.h"

#include <algorithm>
#include <utility>

#include "cores/atari8/atari8_wire.h"
#include "cores/atari8/cart_form.h"
#include "cores/atari8/cart_layout.h"
#include "infra/message_sum.h"

namespace mister::cores::atari8 {

namespace {

constexpr Atari8Address kCartMemory{0x0280'0000};
constexpr proto::FileSize kSecondCart{0x10'0000};
constexpr proto::FileSize kCorinaEeprom{0x10'0000};

constexpr proto::FileSize kCorinaFrom{0x8'0000};
constexpr proto::FileSize kCorinaEepromBytes{0x2000};
constexpr proto::FileSize kCorinaClearBytes{0x8'0000};

constexpr proto::FileSize kActBytes{proto::kMailboxActBytes};

constexpr proto::FileSize kCorina512Image{0x8'2000};
constexpr proto::FileSize kCorina1024Image{0x10'2000};

constexpr proto::FileSize kKib{0x400};
constexpr proto::FileOffset kCarChecksum{8};

[[nodiscard]] Atari8Address cart_base(CartSlot slot) noexcept {
    switch (slot) {
        case CartSlot::First:
            break;
        case CartSlot::Second:
            return kCartMemory.advanced(kSecondCart);
    }
    return kCartMemory;
}

[[nodiscard]] proto::MailboxAct upload(Atari8Address at, proto::MailboxActLen len) noexcept {
    return infra::make<proto::MailboxAct>(proto::MailboxAct::AddressedUpload{
        .address = at.aux(),
        .index = wire::kDmaIndex,
        .len = len
    });
}

[[nodiscard]] proto::MailboxAct download(Atari8Address at) noexcept {
    return infra::make<proto::MailboxAct>(proto::MailboxAct::AddressedDownload{
        .address = at.aux(),
        .index = wire::kDmaIndex,
        .len = proto::MailboxActLen::whole()
    });
}

}

void CartWriteBack::begin_core() noexcept {
    TASTY_SEAT_BODY(CartWriteBack);
    first_.reset();
    second_.reset();
    phase_ = Phase::Idle;
    next_poll_ = {};
    dirty_ = false;
    dirty_second_ = false;
    save_.reset();
}

void CartWriteBack::hold(CartSlot slot, Held held) noexcept {
    TASTY_SEAT_BODY(CartWriteBack);
    const bool corina = held.match.layout == CartLayout::Corina;
    if (save_ && save_->slot == slot)
        save_.reset();
    held_(slot) = std::move(held);
    if (!corina) {
        if (phase_ == Phase::Save && !save_)
            phase_ = Phase::Idle;
        return;
    }
    moving_ = slot;
    moved_ = {};
    phase_ = Phase::MoveRead;
}

void CartWriteBack::drop(CartSlot slot) noexcept {
    TASTY_SEAT_BODY(CartWriteBack);
    held_(slot).reset();
    if (save_ && save_->slot == slot) {
        save_.reset();
        phase_ = Phase::Idle;
    }
    if (moving_ == slot
        && (phase_ == Phase::MoveRead || phase_ == Phase::MoveWrite || phase_ == Phase::MoveClear))
        phase_ = Phase::Idle;
}

CartSelects CartWriteBack::selects() const noexcept {
    TASTY_SEAT_BODY(CartWriteBack);
    return CartSelects{
        .first = first_ ? first_->match.mode : CartMode::kOff,
        .second = second_ ? second_->match.mode : CartMode::kOff
    };
}

proto::MailboxAct CartWriteBack::next(ActBytes bytes, NotBefore) noexcept {
    TASTY_SEAT_BODY(CartWriteBack);
    if (phase_ == Phase::Idle && holds_any_())
        phase_ = Phase::Poll;
    switch (phase_) {
        case Phase::Idle:
            return {};
        case Phase::Poll: {
            proto::MailboxRun run{bytes};
            (void)run.add(wire::get(wire::ReadRegister::Flash));
            return infra::make<proto::MailboxAct>(
                proto::MailboxAct::CommandRun{.not_before = next_poll_, .commands = run.commands()}
            );
        }
        case Phase::MoveRead:
        case Phase::MoveWrite:
        case Phase::MoveClear:
            return move_act_(bytes);
        case Phase::Save:
            return save_act_();
    }
    return {};
}

void CartWriteBack::settled(
    const proto::MailboxResult& result,
    ActBytes bytes,
    NotBefore now
) noexcept {
    TASTY_SEAT_BODY(CartWriteBack);
    if (result.end != proto::MailboxResult::End::Done)
        return;
    switch (phase_) {
        case Phase::Idle:
            return;
        case Phase::Poll:
            phase_ = Phase::Idle;
            next_poll_ = now + kPollPeriod;
            if (result.replies.kept != proto::ReplyCount{})
                read_flash_(CartFlash{result.replies.replies[proto::ReplyIndex{}]}, now);
            return;
        case Phase::MoveRead:
            std::ranges::copy(bytes, piece_.begin());
            phase_ = Phase::MoveWrite;
            return;
        case Phase::MoveWrite:
            moved_ += kActBytes;
            phase_ = moved_ == kCorinaEepromBytes ? Phase::MoveClear : Phase::MoveRead;
            if (phase_ == Phase::MoveClear)
                moved_ = {};
            return;
        case Phase::MoveClear:
            moved_ += kActBytes;
            if (moved_ == kCorinaClearBytes)
                phase_ = Phase::Idle;
            return;
        case Phase::Save:
            if (!save_) {
                phase_ = Phase::Idle;
                return;
            }
            saved_(piece_len(*save_).of(bytes));
            return;
    }
}

proto::MailboxActLen CartWriteBack::piece_len(const Save& s) noexcept {
    const proto::FileSize left = s.end - s.at;
    return left < kActBytes ? proto::MailboxActLen{left.file_word<std::uint16_t>()}
                            : proto::MailboxActLen::whole();
}

std::optional<CartWriteBack::Held>& CartWriteBack::held_(CartSlot slot) noexcept {
    switch (slot) {
        case CartSlot::First:
            break;
        case CartSlot::Second:
            return second_;
    }
    return first_;
}

bool CartWriteBack::holds_any_() const noexcept {
    return first_.has_value() || second_.has_value();
}

void CartWriteBack::read_flash_(CartFlash flash, NotBefore now) noexcept {
    if (flash.written()) {
        quiet_at_ = now + kAutosaveQuiet;
        dirty_ = true;
        if (flash.second())
            dirty_second_ = true;
    }
    if (!dirty_ || !(flash.save_now() || (flash.autosave() && quiet_at_ <= now)))
        return;
    const CartSlot slot = dirty_second_ ? CartSlot::Second : CartSlot::First;
    dirty_ = false;
    dirty_second_ = false;
    save_ = save_of_(slot);
    if (save_)
        phase_ = Phase::Save;
}

std::optional<CartWriteBack::Save> CartWriteBack::save_of_(CartSlot slot) noexcept {
    const std::optional<Held>& held = held_(slot);
    if (!held || !held->writable || held->file == nullptr)
        return std::nullopt;
    const auto size = held->file->size();
    if (!size)
        return std::nullopt;
    Save s{.slot = slot, .end = proto::kFileStart + *size, .memory = cart_base(slot)};
    s.car = size->bytes_past_whole(kKib) == kCarHeaderBytes;
    s.at = s.car ? proto::kFileStart + kCarHeaderBytes : proto::kFileStart;
    const proto::FileSize body = s.end - s.at;
    if (body == kCorina512Image || body == kCorina1024Image) {
        s.memory = s.memory.advanced(kCorinaEeprom);
        s.at = s.end - kCorinaEepromBytes;
    }
    return s;
}

void CartWriteBack::saved_(std::span<const std::uint8_t> piece) noexcept {
    Held& held = *held_(save_->slot);
    if (!held.file->write_all_at(save_->at, std::as_bytes(piece))) {
        save_.reset();
        phase_ = Phase::Idle;
        return;
    }
    if (save_->car)
        save_->sum.add(piece);
    save_->at += proto::FileSize::of(piece);
    save_->memory = save_->memory.advanced(proto::FileSize::of(piece));
    if (save_->at < save_->end)
        return;
    if (save_->car) {
        std::array<std::byte, 4> sum{};
        save_->sum.store_be(sum);
        (void)held.file->write_all_at(kCarChecksum, sum);
    }
    (void)held.file->flush();
    save_.reset();
    phase_ = Phase::Idle;
}

proto::MailboxAct CartWriteBack::move_act_(ActBytes bytes) const noexcept {
    const Atari8Address from = cart_base(moving_).advanced(kCorinaFrom);
    switch (phase_) {
        case Phase::MoveRead:
            return upload(from.advanced(moved_), proto::MailboxActLen::whole());
        case Phase::MoveWrite:
            std::ranges::copy(piece_, bytes.begin());
            return download(cart_base(moving_).advanced(kCorinaEeprom).advanced(moved_));
        case Phase::MoveClear:
            std::ranges::fill(bytes, std::uint8_t{0});
            return download(from.advanced(moved_));
        case Phase::Idle:
        case Phase::Poll:
        case Phase::Save:
            break;
    }
    return {};
}

proto::MailboxAct CartWriteBack::save_act_() const noexcept {
    if (!save_)
        return {};
    return upload(save_->memory, piece_len(*save_));
}

static_assert(
    kCorinaEepromBytes / kActBytes * kActBytes == kCorinaEepromBytes
        && kCorinaClearBytes / kActBytes * kActBytes == kCorinaClearBytes,
    "atari800.cpp:701-707: 8 KiB moved, 512 KiB cleared, in whole act buffers"
);

}
