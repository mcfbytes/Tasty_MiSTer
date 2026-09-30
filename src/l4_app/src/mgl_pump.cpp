// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mgl_pump.h"

#include "app/identity_latch.h"
#include "app/info_sink.h"
#include "app/rbf_resolve.h"
#include "app/session_identity.h"
#include "infra/message_sum.h"
#include "svc/search_policy.h"
#include "svc/xml_scan.h"

#include <chrono>
#include <cstring>
#include <span>
#include <string>

namespace mister::app {

namespace xml = svc::xml;
namespace {

using namespace std::chrono_literals;

void split_rbf(std::string_view want, std::string_view& dir, std::string_view& stem) {
    const std::size_t slash = want.rfind('/');
    if (slash == std::string_view::npos) {
        dir = {};
        stem = want;
        return;
    }
    dir = want.substr(0, slash);
    stem = want.substr(slash + 1);
}

std::string games_home(const svc::Vfs& vfs, std::string_view dir) {
    const svc::SearchPolicy prefixed{svc::search::kPrefixed, true};
    if (auto p = vfs.resolve(dir, prefixed); p && vfs.dir_exists(*p)) return *p;
    std::string games = "games/";
    games.append(dir);
    if (auto p = vfs.resolve(games, prefixed); p && vfs.dir_exists(*p)) return *p;
    return games;
}

}  // namespace

void MglPump::bind_homes() {
    if (identity_ == nullptr) return;
    SessionIdentity id{};
    bool bound = identity_->copy(id);
    if (bound) {

        const std::string file_home = games_home(*vfs_, home_name(id));
        const std::string image_home =
            id.cue_dir != nullptr ? games_home(*vfs_, id.cue_dir) : file_home;
        bound = player_.set_homes(file_home, image_home);
    }
    if (bound) return;

    ++stats_.homes_unbound;
    if (diag_ != nullptr) {
        diag_->appendf("{\"t\":\"ev\",\"k\":\"MglHomeUnbound\",\"n\":%u}", stats_.homes_unbound);
    }
}

CoreScope MglPump::sample_scope() {
    if (conf_cell_ == nullptr || conf_cell_->sample_into(conf_scratch_) == 0) return {};
    return CoreScope{conf_scratch_.gen};
}

bool MglPump::take_playlist(std::string_view path) noexcept {
    TASTY_SEAT_BODY(MglPump);
    const PublishOnExit publish_on_exit{this};

    const std::string_view p = path;
    const std::size_t n = p.size() < kPathMax ? p.size() : kPathMax - 1;
    std::memcpy(latch_, p.data(), n);
    latch_[n] = '\0';
    latch_len_ = static_cast<std::uint16_t>(n);

    if (pending_) ++stats_.superseded;
    pending_ = true;
    ++stats_.routed;
    return true;
}

void MglPump::on_core_loaded(CorrelationTag tag, bool mgl_capable) {
    const PublishOnExit publish_on_exit{this};
    check_invariant();

    if (state_ == State::Playing) {
        abandon(InfoId::CoreLoadFailed, Errc::negotiation);
        return;
    }

    if (!load_tag_ || tag != load_tag_) return;
    load_tag_ = kUncaused;
    if (state_ != State::AwaitingCore) return;

    if (!mgl_capable) {
        abandon(InfoId::None, Errc::negotiation);
        return;
    }

    if (auto r = player_.parse(std::string_view{doc_, doc_len_}, true); !r) {
        abandon(InfoId::CoreLoadFailed, r.error().code);
        return;
    }
    bind_homes();
    player_.set_scope(sample_scope());
    if (player_.count() == 0) {

        enter(State::Idle);
        return;
    }

    for (std::uint8_t i = 0; i < player_.count(); ++i) {

        if (const auto ld = infra::as<MglItem::Load>(player_.item(i));
            ld && ld->slot == MglItem::Slot::File) {
            ++stats_.file_items;
        }
    }
    player_.arm(*clock_);
    busy_since_.reset();
    enter(State::Playing);
    ++stats_.armed;
}

void MglPump::on_session_over(CorrelationTag tag) {
    const PublishOnExit publish_on_exit{this};
    check_invariant();
    if (state_ == State::Playing) {
        abandon(InfoId::CoreLoadFailed, Errc::core_load);
        return;
    }
    if (!load_tag_ || tag != load_tag_) return;
    load_tag_ = kUncaused;
    if (state_ == State::AwaitingCore) {
        abandon(InfoId::CoreLoadFailed, Errc::core_load);
    }
}

void MglPump::on_request_refused(CorrelationTag tag, Errc why) {
    const PublishOnExit publish_on_exit{this};
    check_invariant();

    if (why == Errc::would_block && (state_ == State::Playing || state_ == State::Idle)) {
        replay_busy_(tag);
        return;
    }

    if (state_ != State::AwaitingCore) return;
    if (!load_tag_ || tag != load_tag_) return;
    load_tag_ = kUncaused;

    if (why == Errc::cancelled) {
        abandon(InfoId::None, Errc::cancelled);
        return;
    }
    ++stats_.load_retries;
    if (retries_used_ >= kMaxLoadRetries || retry_deadline_.expired(*clock_)) {
        abandon(InfoId::CoreLoadFailed, Errc::negotiation);
        return;
    }
    ++retries_used_;
    enter(State::PendingRead);
}

void MglPump::replay_busy_(CorrelationTag tag) {
    const bool first = !busy_since_;
    if (first) busy_since_ = os::Deadline::in(*clock_, std::chrono::milliseconds{kBusyBoundMs});
    if (!first && busy_since_->expired(*clock_)) {
        if (!player_.owns(tag)) return;
        if (state_ == State::Idle) ++stats_.abandoned;
        abandon(InfoId::CoreLoadFailed, Errc::timeout);
        return;
    }
    if (!player_.replay_refused(tag, *clock_)) return;
    ++stats_.busy_replays;
    state_ = State::Playing;
    check_invariant();
}

void MglPump::enter(State s) noexcept {

    if (s != State::Playing) {
        player_ = MglPlayer{};
        player_.set_link_tx(link_tx_);
        player_.set_asks(asks_);
    }
    state_ = s;
    check_invariant();
}

void MglPump::check_invariant() const noexcept {
#ifndef NDEBUG
    if ((state_ == State::Playing) != !player_.done()) {
        fatal(Error{Errc::negotiation, ERR_SITE(), static_cast<std::uint32_t>(state_)},
              "MglPump: player_ non-Done and state_ == Playing must agree");
    }
#endif
}

void MglPump::tick() {
    const PublishOnExit publish_on_exit{this};
    check_invariant();

    if (pending_) {
        begin_read();
        return;
    }

    switch (state_) {
        case State::PendingRead:

            if (retry_deadline_.expired(*clock_)) {
                abandon(InfoId::CoreLoadFailed, Errc::negotiation);
                return;
            }
            (void)publish_load();
            return;

        case State::AwaitingCore:

            if (arm_deadline_.expired(*clock_)) {
                ++stats_.arm_timeouts;
                abandon(InfoId::CoreLoadFailed, Errc::timeout);
            }
            return;

        case State::Playing:
            pump_advance();
            return;

        case State::Idle:
            return;
    }
}

void MglPump::begin_read() {
    pending_ = false;
    const std::string_view path{latch_, latch_len_};

    if (state_ != State::Idle) ++stats_.superseded;
    enter(State::Idle);

    load_tag_ = kUncaused;
    retries_used_ = 0;
    doc_len_ = 0;
    rbf_len_ = 0;
    want_len_ = 0;

    if (path.empty()) {
        ++stats_.read_failures;
        last_error_ = Errc::bad_format;
        if (info_ != nullptr) info_->info(InfoId::CoreLoadFailed);
        last_info_ = InfoId::CoreLoadFailed;
        return;
    }

    auto f = vfs_->open(path, svc::OpenMode::ReadWhole);
    if (!f) {
        ++stats_.read_failures;
        last_error_ = f.error().code;
        if (info_ != nullptr) info_->info(InfoId::CoreLoadFailed);
        last_info_ = InfoId::CoreLoadFailed;
        return;
    }
    auto sz = (*f)->size();
    if (!sz || sz->v == 0 || sz->v > kMglDocMax) {

        ++stats_.read_failures;
        last_error_ = sz ? Errc::bad_format : sz.error().code;
        if (info_ != nullptr) info_->info(InfoId::CoreLoadFailed);
        last_info_ = InfoId::CoreLoadFailed;
        return;
    }
    auto got = (*f)->read_at(0, std::span<std::byte>(reinterpret_cast<std::byte*>(doc_),
                                                     static_cast<std::size_t>(sz->v)));
    if (!got || *got == 0) {
        ++stats_.read_failures;
        last_error_ = got ? Errc::io : got.error().code;
        if (info_ != nullptr) info_->info(InfoId::CoreLoadFailed);
        last_info_ = InfoId::CoreLoadFailed;
        return;
    }
    doc_len_ = static_cast<std::uint32_t>(*got);

    const std::string_view want = xml::rbf_text(std::string_view{doc_, doc_len_});
    if (want.empty()) {

        ++stats_.no_rbf;
        last_error_ = Errc::not_found;
        if (info_ != nullptr) info_->info(InfoId::CoreNotFound);
        last_info_ = InfoId::CoreNotFound;
        doc_len_ = 0;
        return;
    }
    want_len_ = static_cast<std::uint16_t>(want.size() < kPathMax ? want.size() : kPathMax - 1);
    std::memcpy(want_, want.data(), want_len_);
    want_[want_len_] = '\0';

    if (auto r = resolve_rbf(std::string_view{want_, want_len_}); !r) {
        ++stats_.no_rbf;
        last_error_ = r.error().code;
        if (info_ != nullptr) info_->info(InfoId::CoreNotFound);
        last_info_ = InfoId::CoreNotFound;
        doc_len_ = 0;
        return;
    }

    enter(State::PendingRead);
    retry_deadline_ = os::Deadline::in(*clock_, std::chrono::milliseconds{kLoadRetryMs});
    (void)publish_load();
}

bool MglPump::publish_load() {
    UiRequest::LoadCore req{};

    req.xml = XmlKind::Rbf;
    if (asks_ == nullptr || !req.path.assign(std::string_view{rbf_, rbf_len_})) {
        abandon(InfoId::CoreLoadFailed, Errc::bad_format);
        return false;
    }

    const CorrelationTag tag = asks_->push(req);
    if (!tag) {
        ++stats_.load_retries;
        return false;
    }
    ++stats_.loads;
    load_tag_ = tag;
    enter(State::AwaitingCore);
    arm_deadline_ = os::Deadline::in(*clock_, std::chrono::milliseconds{kArmTimeoutMs});
    return true;
}

void MglPump::pump_advance() {

    for (unsigned i = 0; i < kAdvanceCap; ++i) {
        const MglState before = player_.state();
        const std::uint8_t before_item = player_.current();
        const std::uint32_t before_pub = player_.publishes();
        const std::uint32_t before_drop = player_.drops();
        auto r = player_.advance(*clock_);
        stats_.publishes += player_.publishes() - before_pub;
        stats_.item_drops += player_.drops() - before_drop;
        if (!r) {

            abandon(InfoId::CoreLoadFailed, r.error().code);
            return;
        }
        if (player_.done()) {
            state_ = State::Idle;
            check_invariant();
            return;
        }
        if (player_.publishes() != before_pub || player_.drops() != before_drop) {
            return;
        }
        if (player_.state() == before && player_.current() == before_item) {
            return;
        }
    }
}

void MglPump::abandon(InfoId id, Errc code) {
    if (state_ == State::Playing || state_ == State::AwaitingCore) {
        ++stats_.abandoned;
    }
    enter(State::Idle);

    load_tag_ = kUncaused;
    doc_len_ = 0;
    last_error_ = code;
    if (id != InfoId::None) {
        last_info_ = id;
        if (info_ != nullptr) info_->info(id);
    }

    if (diag_ != nullptr) {
        diag_->appendf("{\"t\":\"ev\",\"k\":\"MglAbandoned\","
                       "\"err\":%u,\"info\":%u,\"ab\":%u,\"fi\":%u}",
                       static_cast<unsigned>(code), static_cast<unsigned>(id), stats_.abandoned,
                       stats_.file_items);
    }
}

Ex<void> MglPump::resolve_rbf(std::string_view want) {
    std::string_view dir;
    std::string_view stem;
    split_rbf(want, dir, stem);

    auto found = resolve_rbf_name(*vfs_, dir, stem, false);
    if (!found) return std::unexpected(found.error());
    const std::string_view best = *found;

    std::size_t n = 0;
    if (!dir.empty()) {
        n = dir.size() < kPathMax - 2 ? dir.size() : kPathMax - 2;
        std::memcpy(rbf_, dir.data(), n);
        rbf_[n++] = '/';
    }
    const std::size_t m = best.size() < kPathMax - 1 - n ? best.size() : kPathMax - 1 - n;
    std::memcpy(rbf_ + n, best.data(), m);
    rbf_[n + m] = '\0';
    rbf_len_ = static_cast<std::uint16_t>(n + m);
    return {};
}

}  // namespace mister::app
