// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mgl.h"

#include "app/name_config.h"

#include <cstring>

#include "app/mgl_item_dispatch.h"
#include "infra/ascii.h"
#include "svc/xml_scan.h"
#include "svc/xml_scanner.h"

namespace mister::app {

namespace xml = svc::xml;
using infra::ieq;

namespace {

using namespace std::chrono_literals;

struct RawItem {
    MglItem::Slot slot = MglItem::Slot::File;
    std::uint8_t index = 0;
    std::uint16_t delay_s = 0;
    FixedStr<MglItem::kPathMax, StrFit::Clip> path{};
};

constexpr std::chrono::milliseconds kDefaultResetHold{100};

[[nodiscard]] std::uint32_t parse_ul(std::string_view s) {
    constexpr unsigned kDecimal = 10;
    constexpr unsigned kHex = 16;
    constexpr unsigned kOctal = 8;
    constexpr std::size_t kHexPrefixLen = 2;
    constexpr int kLetterDigitBase = 10;
    constexpr unsigned kNotADigit = kHex;
    constexpr std::uint64_t kUlongSaturate = 0xFFFF'FFFFull;
    std::size_t i = 0;
    while (i < s.size() && xml::is_space(s[i]))
        ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        neg = (s[i] == '-');
        ++i;
    }
    unsigned base = kDecimal;
    if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = kHex;
        i += kHexPrefixLen;
    } else if (i < s.size() && s[i] == '0') {
        base = kOctal;
        ++i;
        if (i == s.size()) {
            return 0;
        }
    }
    std::uint64_t v = 0;
    bool any = false;
    for (; i < s.size(); ++i) {
        const char c = xml::lower(s[i]);
        unsigned d = kNotADigit;
        if (c >= '0' && c <= '9')
            d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = static_cast<unsigned>(c - 'a' + kLetterDigitBase);
        if (d >= base)
            break;
        any = true;
        v = v * base + d;
        if (v > kUlongSaturate)
            v = kUlongSaturate;
    }
    if (!any)
        return 0;

    return neg ? static_cast<std::uint32_t>(0u - static_cast<std::uint32_t>(v))
               : static_cast<std::uint32_t>(v);
}

}

void MglPlayer::reset() noexcept {

    stem_ = {};
    remembered_ = 0;
    remember_failures_ = 0;
    scope_ = {};
    entries_ = {};
    file_home_ = {};
    image_home_ = {};
    base_ = PathBase::Home;
    count_ = 0;
    current_ = 0;
    publishes_ = 0;
    drops_ = 0;
    last_tag_ = {};
    armed_ = false;
    state_ = MglState::Done;
    timer_ = os::Deadline::immediate();
}

Ex<void> MglPlayer::parse(std::string_view doc, bool core_supports_mgl) {
    constexpr std::uint32_t kDelayMax = 0xFFFFu;
    constexpr std::uint32_t kHoldMax = 0xFFu;
    constexpr std::uint32_t kLoadIndexMax = 15;
    constexpr unsigned kHasDelay = 0x1u;
    constexpr unsigned kHasType = 0x2u;
    constexpr unsigned kHasIndex = 0x4u;
    constexpr unsigned kHasPath = 0x8u;
    constexpr unsigned kHasAll = kHasDelay | kHasType | kHasIndex | kHasPath;
    reset();

    if (!core_supports_mgl) {

        return {};
    }

    bool inside_mgl = false;
    xml::XmlScanner scan{doc};
    xml::Attr attrs[xml::kMaxAttrs];

    std::array<RawItem, kMaxItems> raw{};

    while (const auto tag = scan.next_tag(attrs)) {
        if (ieq(tag->name, "mistergamedescription")) {
            inside_mgl = !tag->closing;
            continue;
        }
        if (tag->closing)
            continue;

        if (!inside_mgl || count_ >= kMaxItems)
            continue;

        RawItem& it = raw[count_];

        if (ieq(tag->name, "file")) {
            unsigned valid = 0;
            for (std::size_t i = 0; i < tag->attr_count; ++i) {
                const xml::Attr& a = attrs[i];
                std::uint32_t n = 0;
                if (ieq(a.name, "delay")) {

                    n = parse_ul(a.value);
                    it.delay_s = static_cast<std::uint16_t>(n > kDelayMax ? kDelayMax : n);
                    valid |= kHasDelay;
                } else if (ieq(a.name, "type")) {

                    if (ieq(a.value, "s")) {
                        it.slot = MglItem::Slot::Image;
                        valid |= kHasType;
                    } else if (ieq(a.value, "f")) {
                        it.slot = MglItem::Slot::File;
                        valid |= kHasType;
                    }
                } else if (ieq(a.name, "index")) {
                    n = parse_ul(a.value);

                    if (n > kLoadIndexMax)
                        continue;
                    it.index = static_cast<std::uint8_t>(n);
                    valid |= kHasIndex;
                } else if (ieq(a.name, "path")) {
                    (void)it.path.assign(a.value);
                    valid |= kHasPath;
                }
            }

            if (valid == kHasAll) {
                entries_[count_].item = infra::make<MglItem>(MglItem::Load{
                    .slot = it.slot,
                    .index = it.index,
                    .delay_s = it.delay_s,
                    .path = it.path
                });
                ++count_;
            }
        } else if (ieq(tag->name, "reset")) {
            bool valid = false;
            for (std::size_t i = 0; i < tag->attr_count; ++i) {
                const xml::Attr& a = attrs[i];
                std::uint32_t n = 0;
                if (ieq(a.name, "delay")) {
                    n = parse_ul(a.value);
                    it.delay_s = static_cast<std::uint16_t>(n > kDelayMax ? kDelayMax : n);
                    valid = true;
                } else if (ieq(a.name, "hold")) {
                    n = parse_ul(a.value);

                    it.index = static_cast<std::uint8_t>(n > kHoldMax ? kHoldMax : n);
                }
            }
            if (valid) {
                entries_[count_].item = infra::make<MglItem>(
                    MglItem::Reset{.delay_s = it.delay_s, .hold_s = it.index}
                );
                ++count_;
            }
        }
    }

    state_ = (count_ == 0) ? MglState::Done : MglState::Armed;
    return {};
}

Ex<void> MglPlayer::adopt(const MglItem& item, PathBase base, bool core_supports_mgl) {
    constexpr std::uint32_t kLoadIndexMax = 15;
    TASTY_SEAT_BODY(MglPlayer);
    reset();
    if (!core_supports_mgl)
        return {};

    if (const auto ld = infra::as<MglItem::Load>(item); ld && ld->index > kLoadIndexMax)
        return {};

    base_ = base;
    entries_[0].item = item;
    count_ = 1;
    state_ = MglState::Armed;
    return {};
}

bool MglPlayer::set_homes(std::string_view file_home, std::string_view image_home) noexcept {
    if (file_home_.assign(file_home) && image_home_.assign(image_home))
        return true;
    file_home_.clear();
    image_home_.clear();
    return false;
}

void MglPlayer::arm(const os::IClock& clock) {
    if (armed_ || state_ == MglState::Done)
        return;
    armed_ = true;
    timer_ = os::Deadline::in(clock, std::chrono::seconds{delay_of_(0)});
}

std::uint16_t MglPlayer::delay_of_(std::uint8_t i) const noexcept {
    if (const auto ld = infra::as<MglItem::Load>(entries_[i].item); ld)
        return ld->delay_s;
    if (const auto rs = infra::as<MglItem::Reset>(entries_[i].item); rs)
        return rs->delay_s;
    return 0;
}

std::uint8_t MglPlayer::hold_of_(std::uint8_t i) const noexcept {
    const auto rs = infra::as<MglItem::Reset>(entries_[i].item);
    return rs ? rs->hold_s : 0;
}

void MglPlayer::record_publish(CorrelationTag tag) noexcept {
    if (!tag) {
        ++drops_;
        return;
    }
    ++publishes_;
    last_tag_ = tag;
}

void MglPlayer::record_wire_publish(bool pushed) noexcept {
    if (pushed) {
        ++publishes_;
    } else {
        ++drops_;
    }
}

void MglPlayer::dispatch_current() {
    infra::dispatch<MglItemRoutes>(entries_[current_].item, *this);
}

Ex<void> MglPlayer::publish_load_() {
    const auto ld = infra::as<MglItem::Load>(entries_[current_].item);
    if (!ld || ld->path.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), current_});
    }

    const std::string_view p = ld->path.view();
    const PathText& home = ld->slot == MglItem::Slot::File ? file_home_ : image_home_;
    PathText path{};
    const bool fits = (p.front() == '/' || home.empty() || base_ == PathBase::Card)
        ? path.assign(p)
        : path.assign(home.view()) && path.append("/") && path.append(p);
    if (!fits) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), current_});
    }
    const CorrelationTag tag = ld->slot == MglItem::Slot::File
        ? asks_.push(
              UiRequest::LoadFileByDigit{
                  .digit = proto::FileSlotDigit{ld->index},
                  .scope = scope_,
                  .path = path
              }
          )
        : asks_.push(
              UiRequest::MountImage{
                  .index = proto::IoIndex{ld->index},
                  .scope = scope_,
                  .path = path
              }
          );
    record_publish(tag);
    entries_[current_].tag = tag;

    if (const auto& at = entries_[current_].remember; tag && at) {
        if (names_.save_path(stem_, at->slot, at->ioctl_index, path.view()))
            ++remembered_;
        else
            ++remember_failures_;
    }
    return {};
}

void MglPlayer::remember(
    std::uint8_t i,
    const RememberedStem& stem,
    RememberedSlot slot,
    std::uint8_t ioctl_index
) noexcept {
    if (i >= count_)
        return;
    stem_ = stem;
    entries_[i].remember = RememberAt{slot, ioctl_index};
}

bool MglPlayer::owns(CorrelationTag tag) const noexcept {
    for (std::uint8_t i = 0; tag && i < count_; ++i)
        if (entries_[i].tag == tag)
            return true;
    return false;
}

bool MglPlayer::replay_refused(CorrelationTag tag, const os::IClock& clock) {
    if (!tag)
        return false;
    std::uint8_t i = 0;
    while (i < count_ && entries_[i].tag != tag)
        ++i;
    if (i == count_)
        return false;
    for (std::uint8_t k = i; k < count_; ++k)
        entries_[k].tag = kUncaused;
    current_ = i;
    armed_ = true;
    timer_ = os::Deadline::in(clock, std::chrono::milliseconds{kBusyReplayMs});
    state_ = MglState::Armed;
    return true;
}

Ex<void> MglPlayer::advance(const os::IClock& clock) {
    constexpr std::uint32_t kMillisPerSecond = 1000;
    if (state_ == MglState::Done)
        return {};
    if (!armed_)
        arm(clock);

    switch (state_) {
        case MglState::Armed:

            if (!timer_.expired(clock))
                return {};
            dispatch_current();
            return {};

        case MglState::Dispatching: {

            auto r = publish_load_();
            if (!r) {
                state_ = MglState::Done;
                return r;
            }
            state_ = MglState::Advancing;
            return {};
        }

        case MglState::Advancing:

            ++current_;
            if (current_ < count_) {
                timer_ = os::Deadline::in(clock, std::chrono::seconds{delay_of_(current_)});
                state_ = MglState::Armed;
            } else {
                state_ = MglState::Done;
            }
            return {};

        case MglState::ResetAssert: {

            const proto::LinkOp::PulseOption op{.bit = proto::StatusBit{0}};
            record_wire_publish(link_tx_.push(op));

            const std::uint8_t hold_s = hold_of_(current_);
            const auto hold_ms = (hold_s != 0)
                ? std::chrono::milliseconds{hold_s * kMillisPerSecond}
                : kDefaultResetHold;
            timer_ = os::Deadline::in(clock, hold_ms);
            state_ = MglState::ResetRelease;
            return {};
        }

        case MglState::ResetRelease:

            if (!timer_.expired(clock))
                return {};
            state_ = MglState::Advancing;
            return {};

        case MglState::Done:
            return {};
    }
    return {};
}

}
