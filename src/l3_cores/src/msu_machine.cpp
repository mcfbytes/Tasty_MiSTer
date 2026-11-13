// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/msu_machine.h"

#include <algorithm>
#include <optional>

namespace mister::cores {

void MsuMachine::begin_core() noexcept {
    TASTY_SEAT_BODY(MsuMachine);
    close_();
    stem_.clear();
    present_ = false;
    last_req_ = 255;
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
    const proto::MailboxFrame& frame,
    std::span<std::uint8_t, proto::kMailboxActBytes> bytes
) noexcept {
    TASTY_SEAT_BODY(MsuMachine);
    if (!present_)
        return {};
    const auto req = static_cast<std::uint8_t>(frame.w[0]);
    if (req == last_req_)
        return {};
    last_req_ = req;
    const std::uint32_t data = frame.w[2] | (std::uint32_t{frame.w[3]} << 16);
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
            return track_(TrackNumber{data});
        case msu::Request::Seek:
            seek_(SectorIndex{data});
            return next_(bytes);
        case msu::Request::Next:
            return next_(bytes);
    }

    return {};
}

proto::MailboxAct MsuMachine::track_(TrackNumber track) noexcept {
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

void MsuMachine::seek_(SectorIndex sector) noexcept {
    cursor_ = msu::TrackCursor{sector.byte_of()};
    ahead_.seek(cursor_);
}

proto::MailboxAct MsuMachine::next_(
    std::span<std::uint8_t, proto::kMailboxActBytes> bytes
) noexcept {
    std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
    if (file_ != nullptr) {
        const std::size_t got = ahead_.take(*file_, cursor_, bytes).size();
        cursor_ += proto::FileSize{got};
    }
    ++counts_.sectors;
    const proto::MailboxAct::Download reply{
        .index = proto::WideIoIndex{msu::kAudioIndex},
        .len = static_cast<std::uint16_t>(msu::kSectorBytes)
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
