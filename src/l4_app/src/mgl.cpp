// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mgl.h"

#include "app/name_config.h"

#include <cstring>

#include "app/mgl_item_dispatch.h"
#include "svc/xml_scan.h"

namespace mister::app {

namespace xml = svc::xml;

namespace {

using namespace std::chrono_literals;

struct RawItem {
    MglItem::Slot slot = MglItem::Slot::File;
    std::uint8_t index = 0;
    std::uint16_t delay_s = 0;
    FixedStr<MglItem::kPathMax, StrFit::Clip> path{};
};

bool parse_ul(std::string_view s, std::uint32_t& out) {
    std::size_t i = 0;
    while (i < s.size() && xml::is_space(s[i]))
        ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        neg = (s[i] == '-');
        ++i;
    }
    unsigned base = 10;
    if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = 16;
        i += 2;
    } else if (i < s.size() && s[i] == '0') {
        base = 8;
        ++i;
        if (i == s.size()) {
            out = 0;
            return true;
        }
    }
    std::uint64_t v = 0;
    bool any = false;
    for (; i < s.size(); ++i) {
        const char c = xml::lower(s[i]);
        unsigned d = 16;
        if (c >= '0' && c <= '9')
            d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = static_cast<unsigned>(c - 'a' + 10);
        if (d >= base) break;
        any = true;
        v = v * base + d;
        if (v > 0xFFFFFFFFull) v = 0xFFFFFFFFull;
    }
    if (!any) return false;

    out = neg ? static_cast<std::uint32_t>(0u - static_cast<std::uint32_t>(v))
              : static_cast<std::uint32_t>(v);
    return true;
}

}  // namespace

void MglPlayer::reset() noexcept {

    remember_.fill(std::nullopt);
    stem_ = {};
    remembered_ = 0;
    remember_failures_ = 0;
    scope_ = {};
    items_ = {};
    file_home_ = {};
    image_home_ = {};
    count_ = 0;
    current_ = 0;
    publishes_ = 0;
    drops_ = 0;
    last_tag_ = {};
    tags_.fill({});
    armed_ = false;
    state_ = MglState::Done;
    timer_ = os::Deadline::immediate();
}

Ex<void> MglPlayer::parse(std::string_view doc, bool core_supports_mgl) {
    reset();

    if (!core_supports_mgl) {

        return {};
    }

    bool inside_mgl = false;
    std::size_t pos = 0;
    xml::Tag tag{};
    xml::Attr attrs[xml::kMaxAttrs];
    std::size_t nattrs = 0;

    std::array<RawItem, kMaxItems> raw{};

    while (xml::next_tag(doc, pos, tag, attrs, nattrs)) {
        if (xml::iequal(tag.name, "mistergamedescription")) {
            inside_mgl = !tag.closing;
            continue;
        }
        if (tag.closing) continue;

        if (!inside_mgl || count_ >= kMaxItems) continue;

        RawItem& it = raw[count_];

        if (xml::iequal(tag.name, "file")) {
            unsigned valid = 0;
            for (std::size_t i = 0; i < nattrs; ++i) {
                const xml::Attr& a = attrs[i];
                std::uint32_t n = 0;
                if (xml::iequal(a.name, "delay")) {

                    (void)parse_ul(a.value, n);
                    it.delay_s = static_cast<std::uint16_t>(n > 0xFFFFu ? 0xFFFFu : n);
                    valid |= 0x1u;
                } else if (xml::iequal(a.name, "type")) {

                    if (xml::iequal(a.value, "s")) {
                        it.slot = MglItem::Slot::Image;
                        valid |= 0x2u;
                    } else if (xml::iequal(a.value, "f")) {
                        it.slot = MglItem::Slot::File;
                        valid |= 0x2u;
                    }
                } else if (xml::iequal(a.name, "index")) {
                    (void)parse_ul(a.value, n);

                    if (n > 15u) continue;
                    it.index = static_cast<std::uint8_t>(n);
                    valid |= 0x4u;
                } else if (xml::iequal(a.name, "path")) {
                    (void)it.path.assign(a.value);
                    valid |= 0x8u;
                }
            }

            if (valid == 0xFu) {
                items_[count_] = infra::make<MglItem>(MglItem::Load{
                    .slot = it.slot, .index = it.index, .delay_s = it.delay_s, .path = it.path});
                ++count_;
            }
        } else if (xml::iequal(tag.name, "reset")) {
            bool valid = false;
            for (std::size_t i = 0; i < nattrs; ++i) {
                const xml::Attr& a = attrs[i];
                std::uint32_t n = 0;
                if (xml::iequal(a.name, "delay")) {
                    (void)parse_ul(a.value, n);
                    it.delay_s = static_cast<std::uint16_t>(n > 0xFFFFu ? 0xFFFFu : n);
                    valid = true;
                } else if (xml::iequal(a.name, "hold")) {
                    (void)parse_ul(a.value, n);

                    it.index = static_cast<std::uint8_t>(n > 0xFFu ? 0xFFu : n);
                }
            }
            if (valid) {
                items_[count_] =
                    infra::make<MglItem>(MglItem::Reset{.delay_s = it.delay_s, .hold_s = it.index});
                ++count_;
            }
        }
    }

    state_ = (count_ == 0) ? MglState::Done : MglState::Armed;
    return {};
}

bool MglPlayer::set_homes(std::string_view file_home, std::string_view image_home) noexcept {
    if (file_home_.assign(file_home) && image_home_.assign(image_home)) return true;
    file_home_.clear();
    image_home_.clear();
    return false;
}

void MglPlayer::arm(const os::IClock& clock) {
    if (armed_ || state_ == MglState::Done) return;
    armed_ = true;
    timer_ = os::Deadline::in(clock, std::chrono::seconds{delay_of_(0)});
}

std::uint16_t MglPlayer::delay_of_(std::uint8_t i) const noexcept {
    if (const auto ld = infra::as<MglItem::Load>(items_[i]); ld) return ld->delay_s;
    if (const auto rs = infra::as<MglItem::Reset>(items_[i]); rs) return rs->delay_s;
    return 0;
}

std::uint8_t MglPlayer::hold_of_(std::uint8_t i) const noexcept {
    const auto rs = infra::as<MglItem::Reset>(items_[i]);
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

void MglPlayer::dispatch_current() { infra::dispatch<MglItemRoutes>(items_[current_], *this); }

Ex<void> MglPlayer::publish_load_() {
    const auto ld = infra::as<MglItem::Load>(items_[current_]);
    if (!ld || ld->path.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), current_});
    }

    const std::string_view p = ld->path.view();
    const PathText& home = ld->slot == MglItem::Slot::File ? file_home_ : image_home_;
    PathText path{};
    const bool fits = (p.front() == '/' || home.empty())
                          ? path.assign(p)
                          : path.assign(home.view()) && path.append("/") && path.append(p);
    if (!fits) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), current_});
    }
    const CorrelationTag tag =
        ld->slot == MglItem::Slot::File
            ? asks_.push(UiRequest::LoadFileByDigit{
                  .digit = proto::FileSlotDigit{ld->index}, .scope = scope_, .path = path})
            : asks_.push(UiRequest::MountImage{
                  .index = proto::IoIndex{ld->index}, .scope = scope_, .path = path});
    record_publish(tag);
    tags_[current_] = tag;

    if (const auto& at = remember_[current_]; tag && at) {
        if (names_.save_path(stem_, at->slot, at->ioctl_index, path.view()))
            ++remembered_;
        else
            ++remember_failures_;
    }
    return {};
}

void MglPlayer::remember(std::uint8_t i, const RememberedStem& stem, RememberedSlot slot,
                         std::uint8_t ioctl_index) noexcept {
    if (i >= count_) return;
    stem_ = stem;
    remember_[i] = RememberAt{slot, ioctl_index};
}

bool MglPlayer::owns(CorrelationTag tag) const noexcept {
    for (std::uint8_t i = 0; tag && i < count_; ++i)
        if (tags_[i] == tag) return true;
    return false;
}

bool MglPlayer::replay_refused(CorrelationTag tag, const os::IClock& clock) {
    if (!tag) return false;
    std::uint8_t i = 0;
    while (i < count_ && tags_[i] != tag)
        ++i;
    if (i == count_) return false;
    for (std::uint8_t k = i; k < count_; ++k)
        tags_[k] = kUncaused;
    current_ = i;
    armed_ = true;
    timer_ = os::Deadline::in(clock, std::chrono::milliseconds{kBusyReplayMs});
    state_ = MglState::Armed;
    return true;
}

Ex<void> MglPlayer::advance(const os::IClock& clock) {
    if (state_ == MglState::Done) return {};
    if (!armed_) arm(clock);

    switch (state_) {
        case MglState::Armed:

            if (!timer_.expired(clock)) return {};
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
            const auto hold_ms = (hold_s != 0) ? std::chrono::milliseconds{hold_s * 1000}
                                               : std::chrono::milliseconds{100};
            timer_ = os::Deadline::in(clock, hold_ms);
            state_ = MglState::ResetRelease;
            return {};
        }

        case MglState::ResetRelease:

            if (!timer_.expired(clock)) return {};
            state_ = MglState::Advancing;
            return {};

        case MglState::Done:
            return {};
    }
    return {};
}

}  // namespace mister::app
