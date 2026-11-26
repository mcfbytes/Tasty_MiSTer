// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/ide/ide_act_wire.h"

#include <algorithm>
#include <limits>
#include <span>
#include <variant>

#include "infra/affine.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/span_cut.h"
#include "proto/uio_opcodes.h"

namespace mister::cores::ide {

namespace {

constexpr hal::LinkWord kDataOut = proto::word(proto::UioOp::DmaWrite);
constexpr hal::LinkWord kDataIn = proto::word(proto::UioOp::DmaRead);

constexpr infra::ByteCount kMostPerAct{proto::kDiskActBytes};
constexpr infra::ByteCount kWordBytes{sizeof(hal::LinkWord)};

static_assert(
    proto::kDiskActBytes <= std::numeric_limits<std::uint16_t>::max(),
    "an act's length field carries one block"
);

constexpr std::uint16_t wire_len(infra::ByteCount n) noexcept {
    return static_cast<std::uint16_t>(n.v);
}

}

void IdeActWire::begin(proto::DiskUnit unit) noexcept {
    TASTY_SEAT_BODY(IdeActWire);
    unit_ = unit;
    first_ = true;
    clear();
}

void IdeActWire::clear() noexcept {
    TASTY_SEAT_BODY(IdeActWire);
    asked_ = 0;
    verb_ = 0;
    at_ = {};
    in_piece_ = {};
    receive_done_ = false;
}

void IdeActWire::ask_(const Verb& verb) noexcept {
    if (asked_ >= verbs_.size())
        fatal(Error{Errc::contract, ERR_SITE(), 0}, "IdeActWire: an answer past its verb bound");
    verbs_[asked_] = verb;
    ++asked_;
}

proto::DiskAddress IdeActWire::address_(IdeRegister reg) const noexcept {
    return row_.base + reg.offset();
}

infra::ByteCount IdeActWire::piece_of_(infra::ByteOffset at, infra::ByteCount whole) noexcept {
    const infra::ByteCount left = infra::affine::end_of<infra::ByteOffset>(whole) - at;
    return left < kMostPerAct ? left : kMostPerAct;
}

void IdeActWire::write_task_file(const IdeTaskFile& regs) noexcept {
    TASTY_SEAT_BODY(IdeActWire);
    ask_(TaskFile{.regs = regs});
}

void IdeActWire::send_data(std::span<const std::byte> data) noexcept {
    TASTY_SEAT_BODY(IdeActWire);
    if (data.empty())
        return;
    ask_(Send{.bytes = data});
}

void IdeActWire::receive_data(std::span<std::byte> into) noexcept {
    TASTY_SEAT_BODY(IdeActWire);
    if (into.empty())
        return;
    ask_(Receive{.bytes = into});
}

void IdeActWire::write_reg(IdeRegister reg, hal::LinkWord value) noexcept {
    TASTY_SEAT_BODY(IdeActWire);
    ask_(WriteReg{.reg = reg, .word = value});
}

proto::DiskAct IdeActWire::next(std::span<std::byte, proto::kDiskActBytes> bytes) noexcept {
    TASTY_SEAT_BODY(IdeActWire);

    if (!in_piece_.empty()) {
        const std::size_t piece = in_piece_.size();
        std::copy_n(bytes.begin(), piece, in_piece_.begin());
        in_piece_ = {};

        const Receive& recv = std::get<Receive>(verbs_[verb_]);
        const infra::ByteOffset end = infra::affine::end_of<infra::ByteOffset>(
            infra::ByteCount{recv.bytes.size()}
        );
        at_ += infra::ByteCount{piece};
        if (at_ == end) {
            ++verb_;
            at_ = {};
            receive_done_ = true;
        }
    }
    if (verb_ >= asked_)
        return proto::DiskAct::none();

    const Verb& verb = verbs_[verb_];
    proto::DiskAct act = proto::DiskAct::none();
    if (const auto* task = std::get_if<TaskFile>(&verb)) {

        const IdeTaskFile::Image image = task->regs.packet ? task->regs.encode_packet()
                                                           : task->regs.encode();
        std::ranges::copy(image, bytes.begin());
        act = proto::DiskAct::out(kDataOut, wire_len(kTaskFileBytes))
                  .at(address_(IdeRegister::kTaskFile));
        ++verb_;
    } else if (const auto* send = std::get_if<Send>(&verb)) {
        const infra::ByteCount whole{send->bytes.size()};
        const infra::ByteCount piece = piece_of_(at_, whole);
        const std::span<const std::byte> part = infra::affine::first(
            infra::affine::from(send->bytes, at_),
            piece
        );
        std::copy(part.begin(), part.end(), bytes.begin());
        act = proto::DiskAct::out(kDataOut, wire_len(piece)).at(address_(IdeRegister::kData));
        at_ += piece;
        if (at_ == infra::affine::end_of<infra::ByteOffset>(whole)) {
            ++verb_;
            at_ = {};
        }
    } else if (const auto* recv = std::get_if<Receive>(&verb)) {
        const infra::ByteCount piece = piece_of_(at_, infra::ByteCount{recv->bytes.size()});
        in_piece_ = infra::affine::first(infra::affine::from(recv->bytes, at_), piece);
        act = proto::DiskAct::in(kDataIn, wire_len(piece)).at(address_(IdeRegister::kData));
    } else if (const auto* reg = std::get_if<WriteReg>(&verb)) {
        reg->word.store_le(bytes.first<sizeof(hal::LinkWord)>());
        act = proto::DiskAct::out(kDataOut, wire_len(kWordBytes)).at(address_(reg->reg));
        ++verb_;
    } else {
        return proto::DiskAct::none();
    }
    if (first_) {
        act = act.with_unit(unit_);
        first_ = false;
    }
    return act;
}

}
