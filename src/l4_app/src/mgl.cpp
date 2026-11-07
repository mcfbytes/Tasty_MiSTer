// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mgl.h"

#include "app/name_config.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>

#include "app/mgl_item_dispatch.h"
#include "infra/ascii.h"
#include "infra/message_sum.h"
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
    DelaySeconds delay_s{};
    FixedStr<MglItem::kPathMax, StrFit::Clip> path{};
};

constexpr std::chrono::milliseconds kDefaultResetHold{100};

enum class ElementAttr : std::uint8_t { Delay, Type, Index, Path, kCount };

class ElementAttrs {
public:
    void with(ElementAttr a) noexcept { seen_[infra::ordinal(a)] = true; }

    [[nodiscard]] bool has(ElementAttr a) const noexcept { return seen_[infra::ordinal(a)]; }

    [[nodiscard]] bool all() const noexcept {
        return std::ranges::all_of(seen_, [](bool seen) { return seen; });
    }

private:
    std::array<bool, infra::ordinal(ElementAttr::kCount)> seen_{};
};

constexpr ElementAttr kHasDelay = ElementAttr::Delay;
constexpr ElementAttr kHasType = ElementAttr::Type;
constexpr ElementAttr kHasIndex = ElementAttr::Index;
constexpr ElementAttr kHasPath = ElementAttr::Path;

[[nodiscard]] std::uint32_t parse_ul(std::string_view s);
bool parse_file_item(std::span<const xml::Attr> attrs, RawItem& it);
bool parse_reset_item(std::span<const xml::Attr> attrs, RawItem& it);

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

bool parse_file_item(std::span<const xml::Attr> attrs, RawItem& it) {
    ElementAttrs valid;
    for (const xml::Attr& a : attrs) {
        if (ieq(a.name, "delay")) {

            it.delay_s = DelaySeconds::of(parse_ul(a.value));
            valid.with(kHasDelay);
        } else if (ieq(a.name, "type")) {

            if (ieq(a.value, "s")) {
                it.slot = MglItem::Slot::Image;
                valid.with(kHasType);
            } else if (ieq(a.value, "f")) {
                it.slot = MglItem::Slot::File;
                valid.with(kHasType);
            }
        } else if (ieq(a.name, "index")) {

            const std::uint32_t n = parse_ul(a.value);
            if (!proto::FileSlotDigit::from(n))
                continue;
            it.index = static_cast<std::uint8_t>(n);
            valid.with(kHasIndex);
        } else if (ieq(a.name, "path")) {
            (void)it.path.assign(a.value);
            valid.with(kHasPath);
        }
    }
    return valid.all();
}

bool parse_reset_item(std::span<const xml::Attr> attrs, RawItem& it) {
    ElementAttrs valid;
    for (const xml::Attr& a : attrs) {
        if (ieq(a.name, "delay")) {
            it.delay_s = DelaySeconds::of(parse_ul(a.value));
            valid.with(kHasDelay);
        } else if (ieq(a.name, "hold")) {

            constexpr std::uint32_t kHoldMax = std::numeric_limits<std::uint8_t>::max();
            const std::uint32_t n = parse_ul(a.value);
            it.index = static_cast<std::uint8_t>(n > kHoldMax ? kHoldMax : n);
        }
    }
    return valid.has(kHasDelay);
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
    current_ = MglItemNo{};
    publishes_ = 0;
    drops_ = 0;
    last_tag_ = {};
    armed_ = false;
    state_ = MglState::Done;
    timer_ = os::Deadline::immediate();
}

Ex<void> MglPlayer::parse(std::string_view doc, bool core_supports_mgl) {
    reset();

    if (!core_supports_mgl)
        return {};

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
        const MglItemNo no = MglItemNo::of(count_);
        if (ieq(tag->name, "file")
            && parse_file_item(std::span<const xml::Attr>{attrs, tag->attr_count}, it)) {
            entries_[no].item = infra::make<MglItem>(MglItem::Load{
                .slot = it.slot,
                .index = proto::FileSlotDigit{it.index},
                .delay_s = it.delay_s,
                .path = it.path
            });
            ++count_;
        } else if (
            ieq(tag->name, "reset")
            && parse_reset_item(std::span<const xml::Attr>{attrs, tag->attr_count}, it)
        ) {
            entries_[no].item = infra::make<MglItem>(
                MglItem::Reset{.delay_s = it.delay_s, .hold_s = it.index}
            );
            ++count_;
        }
    }

    state_ = (count_ == 0) ? MglState::Done : MglState::Armed;
    return {};
}

Ex<void> MglPlayer::adopt(const MglItem& item, PathBase base, bool core_supports_mgl) {
    TASTY_SEAT_BODY(MglPlayer);
    reset();
    if (!core_supports_mgl)
        return {};

    const auto ld = load_of(item);
    if (ld && !mgl_index_in_range(ld->index))
        return {};

    base_ = base;
    entries_[MglItemNo{}].item = item;
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
    timer_ = os::Deadline::in(clock, delay_of_(MglItemNo{}).as_seconds());
}

DelaySeconds MglPlayer::delay_of_(MglItemNo i) const noexcept {
    return delay_of(entries_[i].item);
}

std::uint8_t MglPlayer::hold_of_(MglItemNo i) const noexcept {
    const auto rs = reset_of(entries_[i].item);
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
    const auto ld = load_of(entries_[current_].item);
    if (!ld || ld->path.empty()) {
        return std::unexpected(current_.refusal(Errc::bad_format, ERR_SITE()));
    }

    const std::string_view p = ld->path.view();
    const PathText& home = ld->slot == MglItem::Slot::File ? file_home_ : image_home_;
    PathText path{};
    const bool fits = (p.front() == '/' || home.empty() || base_ == PathBase::Card)
        ? path.assign(p)
        : path.assign(home.view()) && path.append("/") && path.append(p);
    if (!fits) {
        return std::unexpected(current_.refusal(Errc::bad_format, ERR_SITE()));
    }
    const CorrelationTag tag = ld->slot == MglItem::Slot::File
        ? asks_.push(UiRequest::LoadFileByDigit{.digit = ld->index, .scope = scope_, .path = path})
        : asks_.push(
              UiRequest::MountImage{
                  .index = mount_index_of(ld->index),
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
    MglItemNo i,
    const RememberedStem& stem,
    RememberedSlot slot,
    proto::IoIndex ioctl_index
) noexcept {
    if (!i.below(count_))
        return;
    stem_ = stem;
    entries_[i].remember = RememberAt{slot, ioctl_index};
}

bool MglPlayer::owns(CorrelationTag tag) const noexcept {
    for (MglItemNo i{}; tag && i.below(count_); i = i.next())
        if (entries_[i].tag == tag)
            return true;
    return false;
}

bool MglPlayer::replay_refused(CorrelationTag tag, const os::IClock& clock) {
    if (!tag)
        return false;
    MglItemNo i{};
    while (i.below(count_) && entries_[i].tag != tag)
        i = i.next();
    if (!i.below(count_))
        return false;
    for (MglItemNo k = i; k.below(count_); k = k.next())
        entries_[k].tag = kUncaused;
    current_ = i;
    armed_ = true;
    timer_ = os::Deadline::in(clock, std::chrono::milliseconds{kBusyReplayMs});
    state_ = MglState::Armed;
    return true;
}

Ex<void> MglPlayer::advance(const os::IClock& clock) {
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

            current_ = current_.next();
            if (current_.below(count_)) {
                timer_ = os::Deadline::in(clock, delay_of_(current_).as_seconds());
                state_ = MglState::Armed;
            } else {
                state_ = MglState::Done;
            }
            return {};

        case MglState::ResetAssert: {

            const proto::LinkOp::PulseOption op{.bit = proto::StatusBit{0}};
            record_wire_publish(link_tx_.push(op));

            const std::uint8_t hold_s = hold_of_(current_);
            const std::chrono::milliseconds hold = hold_s != 0 ? std::chrono::seconds{hold_s}
                                                               : kDefaultResetHold;
            timer_ = os::Deadline::in(clock, hold);
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
