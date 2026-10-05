// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/msu_machine.h"

#include <algorithm>
#include <charconv>
#include <cstring>

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
    size_ = 0;
    cursor_ = 0;
    drop_ahead_(0);
    fill_done_ = true;
}

proto::MailboxAct MsuMachine::serve(
    const proto::MailboxFrame& frame,
    std::span<std::uint8_t, proto::kMailboxActBytes> bytes) noexcept {
    TASTY_SEAT_BODY(MsuMachine);
    if (!present_) return {};
    const auto req = static_cast<std::uint8_t>(frame.w[0] & 0xFFu);
    if (req == last_req_) return {};
    last_req_ = req;
    const std::uint16_t command = frame.w[1];
    const std::uint32_t data = frame.w[2] | (std::uint32_t{frame.w[3]} << 16);
    switch (command) {
        case msu::kReqReset:
            ++counts_.resets;
            return {};
        case msu::kReqTrack:
            return track_(data);
        case msu::kReqSeek:
            seek_(data);
            return next_(bytes);
        case msu::kReqNext:
            return next_(bytes);
        default:
            ++counts_.ignored;
            return {};
    }
}

proto::MailboxAct MsuMachine::track_(std::uint32_t track) noexcept {
    close_();
    ++counts_.tracks;
    char num[12]{};
    const auto [end, ec] = std::to_chars(num, num + sizeof num, track);
    const bool named = ec == std::errc{} && path_.assign(stem_.view()) && path_.append("-") &&
                       path_.append(std::string_view(num, static_cast<std::size_t>(end - num))) &&
                       path_.append(".pcm");
    if (named) {
        if (auto f = vfs_->open(path_.view(), svc::OpenMode::Read)) {
            const auto sz = (*f)->size();
            if (sz && sz->v != 0) {
                file_ = std::move(*f);
                size_ = sz->v;
                fill_at_ = 0;
                fill_done_ = false;
            }
        }
    }
    if (file_ == nullptr) ++counts_.missing;
    proto::MailboxAct act{};
    act.kind = proto::MailboxAct::Kind::Command;
    act.opcode = msu::kCdSet;
    act.words = msu::set_track_size(static_cast<std::uint32_t>(size_));
    return act;
}

void MsuMachine::seek_(std::uint32_t sector) noexcept {
    cursor_ = static_cast<std::uint32_t>(sector * static_cast<std::uint32_t>(msu::kSectorBytes));
    for (std::size_t k = 0; k < held_; ++k) {
        if (ahead_[(head_ + k) % kAhead].off != cursor_) continue;
        head_ = (head_ + k) % kAhead;
        held_ -= k;
        return;
    }
    drop_ahead_(cursor_);
    fill_done_ = file_ == nullptr;
}

void MsuMachine::drop_ahead_(std::uint64_t from) noexcept {
    head_ = 0;
    held_ = 0;
    fill_at_ = from;
}

proto::MailboxAct MsuMachine::next_(
    std::span<std::uint8_t, proto::kMailboxActBytes> bytes) noexcept {
    std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
    if (file_ != nullptr) {
        std::size_t n = 0;
        if (held_ != 0 && ahead_[head_].off == cursor_) {
            const Sector& s = ahead_[head_];
            std::memcpy(bytes.data(), s.bytes.data(), s.got);
            n = s.got;
            head_ = (head_ + 1) % kAhead;
            --held_;
            ++counts_.ahead_hits;
        } else {
            n = read_(cursor_, bytes);
            ++counts_.sync_reads;
            drop_ahead_(cursor_ + n);
            fill_done_ = n < msu::kSectorBytes;
        }
        cursor_ += n;
    }
    ++counts_.sectors;
    proto::MailboxAct act{};
    act.kind = proto::MailboxAct::Kind::Download;
    act.index = proto::WideIoIndex{msu::kAudioIndex};
    act.len = static_cast<std::uint16_t>(msu::kSectorBytes);
    return act;
}

bool MsuMachine::rest() const noexcept {
    TASTY_SEAT_BODY(MsuMachine);
    return file_ == nullptr || fill_done_ || held_ == kAhead || fill_at_ >= size_;
}

void MsuMachine::refill_one() noexcept {
    TASTY_SEAT_BODY(MsuMachine);
    if (rest()) return;
    Sector& s = ahead_[(head_ + held_) % kAhead];
    s.bytes.fill(0);
    s.off = fill_at_;
    const std::size_t n = read_(fill_at_, s.bytes);
    s.got = static_cast<std::uint16_t>(n);
    ++held_;
    ++counts_.ahead_reads;
    fill_at_ += n;
    if (n < msu::kSectorBytes) fill_done_ = true;
}

std::size_t MsuMachine::read_(std::uint64_t off, std::span<std::uint8_t> dst) noexcept {
    std::size_t got = 0;
    while (got < dst.size()) {
        auto r = file_->read_at(off + got, std::as_writable_bytes(dst.subspan(got)));
        if (!r) {
            ++counts_.read_errors;
            break;
        }
        if (*r == 0) break;
        got += *r;
    }
    return got;
}

}  // namespace mister::cores
