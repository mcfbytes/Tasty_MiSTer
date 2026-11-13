// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mgl_player.h"

#include "app/name_config.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <span>

#include "app/mgl_item_dispatch.h"
#include "cores/stock_scan.h"
#include "infra/ascii.h"
#include "infra/message_sum.h"
#include "svc/xml_scan.h"
#include "svc/xml_scanner.h"

namespace mister::app {

namespace xml = svc::xml;
using cores::stock_scan::Radix;
using cores::stock_scan::scan_ul;
using infra::ieq;

namespace {

using namespace std::chrono_literals;

struct RawItem {
    proto::FileRowKind slot = proto::FileRowKind::File;
    proto::FileSlotDigit index{};
    ResetHold hold{};
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

bool parse_file_item(std::span<const xml::Attr> attrs, RawItem& it);
bool parse_reset_item(std::span<const xml::Attr> attrs, RawItem& it);

bool parse_file_item(std::span<const xml::Attr> attrs, RawItem& it) {
    ElementAttrs valid;
    for (const xml::Attr& a : attrs) {
        if (ieq(a.name, "delay")) {

            it.delay_s = DelaySeconds::of(scan_ul(a.value, Radix::Auto).value);
            valid.with(kHasDelay);
        } else if (ieq(a.name, "type")) {

            if (ieq(a.value, "s")) {
                it.slot = proto::FileRowKind::Mount;
                valid.with(kHasType);
            } else if (ieq(a.value, "f")) {
                it.slot = proto::FileRowKind::File;
                valid.with(kHasType);
            }
        } else if (ieq(a.name, "index")) {

            const std::uint32_t n = scan_ul(a.value, Radix::Auto).value;
            const std::optional<proto::FileSlotDigit> digit = proto::FileSlotDigit::from(n);
            if (!digit)
                continue;
            it.index = *digit;
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
            it.delay_s = DelaySeconds::of(scan_ul(a.value, Radix::Auto).value);
            valid.with(kHasDelay);
        } else if (ieq(a.name, "hold")) {

            it.hold = ResetHold::of(scan_ul(a.value, Radix::Auto).value);
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

void MglPlayer::parse(std::string_view doc, bool core_supports_mgl) {
    reset();

    if (!core_supports_mgl)
        return;

    bool inside_mgl = false;
    xml::XmlScanner scan{doc};

    std::array<RawItem, kMaxItems> raw{};

    while (const auto tag = scan.next_tag()) {
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
        if (ieq(tag->name, "file") && parse_file_item(tag->attrs, it)) {
            entries_[no].item = infra::make<MglItem>(
                MglItem::Load{.slot = it.slot, .index = it.index, .path = it.path},
                MglItem::Head{.delay_s = it.delay_s}
            );
            ++count_;
        } else if (ieq(tag->name, "reset") && parse_reset_item(tag->attrs, it)) {
            entries_[no].item = infra::make<MglItem>(
                MglItem::Reset{.hold_s = it.hold},
                MglItem::Head{.delay_s = it.delay_s}
            );
            ++count_;
        }
    }

    state_ = (count_ == 0) ? MglState::Done : MglState::Armed;
}

void MglPlayer::adopt(const MglItem& item, PathBase base, bool core_supports_mgl) {
    TASTY_SEAT_BODY(MglPlayer);
    reset();
    if (!core_supports_mgl)
        return;
    base_ = base;
    entries_[MglItemNo{}].item = item;
    count_ = 1;
    state_ = MglState::Armed;
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
    timer_ = os::Deadline::in(
        clock,
        infra::head_of(entries_[MglItemNo{}].item).delay_s.as_seconds()
    );
}

MglPlayer::Result MglPlayer::on(
    const MglItem::Load& load,
    const MglItem::Head&,
    const os::IClock&
) noexcept {
    TASTY_SEAT_BODY(MglPlayer);
    auto r = publish_load_(load);
    if (r)
        state_ = MglState::Advancing;
    return r;
}

MglPlayer::Result MglPlayer::on(
    const MglItem::Reset& reset,
    const MglItem::Head&,
    const os::IClock& clock
) noexcept {
    TASTY_SEAT_BODY(MglPlayer);
    const proto::LinkOp::PulseOption op{.bit = proto::StatusBit{0}};
    record_wire_publish(link_tx_.push(op));

    const std::chrono::milliseconds hold = reset.hold_s.empty() ? kDefaultResetHold
                                                                : reset.hold_s.as_seconds();
    timer_ = os::Deadline::in(clock, hold);
    state_ = MglState::ResetRelease;
    return {};
}

MglPlayer::Result MglPlayer::misrouted(const MglItem&, const os::IClock&) noexcept {
    TASTY_SEAT_BODY(MglPlayer);
    state_ = MglState::Done;
    return {};
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

Ex<void> MglPlayer::dispatch_current(const os::IClock& clock) {
    return infra::dispatch<MglItemRoutes>(entries_[current_].item, *this, clock);
}

Ex<void> MglPlayer::publish_load_(const MglItem::Load& load) {
    if (load.path.empty()) {
        return std::unexpected(current_.refusal(Errc::bad_format, ERR_SITE()));
    }

    const std::string_view p = load.path.view();
    const PathText& home = load.slot == proto::FileRowKind::File ? file_home_ : image_home_;
    PathText path{};
    const bool fits = (p.front() == '/' || home.empty() || base_ == PathBase::Card)
        ? path.assign(p)
        : path.assign(home.view()) && path.append("/") && path.append(p);
    if (!fits) {
        return std::unexpected(current_.refusal(Errc::bad_format, ERR_SITE()));
    }
    const CorrelationTag tag = load.slot == proto::FileRowKind::File
        ? asks_.push(UiRequest::LoadFileByDigit{.digit = load.index, .scope = scope_, .path = path})
        : asks_.push(
              UiRequest::MountImage{.index = load.index.io_index(), .scope = scope_, .path = path}
          );
    record_publish(tag);
    entries_[current_].tag = tag;

    if (const auto& at = entries_[current_].remember; tag && at) {
        if (names_.save_path(stem_, *at, path.view()))
            ++remembered_;
        else
            ++remember_failures_;
    }
    return {};
}

void MglPlayer::remember(MglItemNo i, const RememberedStem& stem, proto::FileRowId row) noexcept {
    if (!i.below(count_))
        return;
    stem_ = stem;
    entries_[i].remember = row;
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
            state_ = MglState::Dispatching;
            return {};

        case MglState::Dispatching: {

            auto r = dispatch_current(clock);
            if (!r)
                state_ = MglState::Done;
            return r;
        }

        case MglState::Advancing:

            current_ = current_.next();
            if (current_.below(count_)) {
                timer_ = os::Deadline::in(
                    clock,
                    infra::head_of(entries_[current_].item).delay_s.as_seconds()
                );
                state_ = MglState::Armed;
            } else {
                state_ = MglState::Done;
            }
            return {};

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
