// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes/msu_machine.h"

#include <algorithm>
#include <optional>

namespace mister::cores::snes {

void MsuMachine::begin_core() noexcept {
    TASTY_SEAT_BODY(MsuMachine);
    close_();
    stem_.clear();
    present_ = false;
    last_req_.reset();
}

void MsuMachine::rebind(std::string_view stem, bool present) noexcept {
    TASTY_SEAT_BODY(MsuMachine);
    close_();
    present_ = stem_.assign(stem) && present;
}

void MsuMachine::release() noexcept {
    TASTY_SEAT_BODY(MsuMachine);
    close_();
}

void MsuMachine::close_() noexcept {
    file_.reset();
    size_ = {};
    cursor_ = {};
    ahead_.drop(msu::TrackCursor{});
}

proto::MailboxAct MsuMachine::serve(
    proto::MailboxSelectIndex,
    const proto::user_io::PollFrame& frame,
    ActBytes bytes,
    BulkBytes
) noexcept {
    TASTY_SEAT_BODY(MsuMachine);
    if (!present_)
        return {};
    const proto::ReplyWord req = frame.reply(msu::kCounterReply).masked(msu::kCounterByte);
    if (last_req_.has_value() && req == *last_req_)
        return {};
    last_req_ = req;
    const hal::LinkHalves data{
        .low = frame.replies[msu::kDataLowReply],
        .high = frame.replies[msu::kDataHighReply]
    };
    const std::optional<msu::Request> request = msu::request_of(frame);
    if (!request) {
        ++counts_.ignored;
        return {};
    }
    switch (*request) {
        case msu::Request::Reset:
            ++counts_.resets;
            return {};
        case msu::Request::Track:
            return track_(AudioTrack::of(data));
        case msu::Request::Seek:
            seek_(SectorLba::of(data));
            return next_(bytes);
        case msu::Request::Next:
            return next_(bytes);
    }

    return {};
}

proto::MailboxAct MsuMachine::settle(const proto::MailboxResult&, ActBytes, BulkBytes) noexcept {
    TASTY_SEAT_BODY(MsuMachine);
    return {};
}

proto::MailboxAct MsuMachine::track_(AudioTrack track) noexcept {
    close_();
    ++counts_.tracks;
    char num[12]{};
    const std::string_view name = track.name_into(num);
    const bool named = !name.empty()
        && path_.assign(stem_.view())
        && path_.append("-")
        && path_.append(name)
        && path_.append(".pcm");
    if (named) {
        if (auto f = vfs_.open(path_.view(), svc::OpenMode::Read)) {
            const auto sz = (*f)->size();
            if (sz && !sz->empty()) {
                file_ = std::move(*f);
                size_ = *sz;
                ahead_.drop(msu::TrackCursor{});
            }
        }
    }
    if (file_ == nullptr)
        ++counts_.missing;
    const proto::MailboxAct::Command reply{
        .opcode = proto::UioOp::CdSet,
        .words = msu::set_track_size(size_.low_word())
    };
    return infra::make<proto::MailboxAct>(reply);
}

void MsuMachine::seek_(SectorLba sector) noexcept {
    cursor_ = msu::TrackCursor{sector.byte_of()};
    ahead_.seek(cursor_);
}

proto::MailboxAct MsuMachine::next_(ActBytes bytes) noexcept {
    std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
    if (file_ != nullptr) {
        const std::size_t got = ahead_.take(*file_, cursor_, bytes).size();
        cursor_ += proto::FileSize{got};
    }
    ++counts_.sectors;
    static_assert(msu::kSectorSize.block_bytes() == proto::kMailboxActBytes);
    const proto::MailboxAct::Download reply{
        .index = proto::WideIoIndex{msu::kAudioIndex},
        .len = proto::MailboxActLen::whole()
    };
    return infra::make<proto::MailboxAct>(reply);
}

bool MsuMachine::rest() const noexcept {
    TASTY_SEAT_BODY(MsuMachine);
    return file_ == nullptr || ahead_.at_rest(size_);
}

void MsuMachine::refill_one() noexcept {
    TASTY_SEAT_BODY(MsuMachine);
    if (file_ == nullptr)
        return;
    ahead_.refill_one(*file_, size_);
}

}
