// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mgl_pump.h"

#include "app/mgl_slot_remember.h"

#include "app/identity_latch.h"
#include "app/mgl_item_dispatch.h"
#include "app/info_sink.h"
#include "app/rbf_resolve.h"
#include "app/session_identity.h"
#include "infra/byte_view.h"
#include "infra/message_sum.h"
#include "infra/path_parts.h"
#include "proto/conf_str.h"
#include "svc/search_policy.h"
#include "svc/xml_scan.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mister::app {

namespace xml = svc::xml;

namespace {

using namespace std::chrono_literals;

struct RbfName {
    std::string_view dir;
    std::string_view stem;
};

[[nodiscard]] RbfName split_rbf(std::string_view want) {
    return RbfName{.dir = infra::dir_or_empty(want), .stem = infra::basename(want)};
}

std::string games_home(const svc::Vfs& vfs, std::string_view dir) {
    const svc::SearchPolicy prefixed{svc::search::kPrefixed, true};
    if (auto p = vfs.resolve(dir, prefixed); p && vfs.dir_exists(*p))
        return *p;
    std::string games = "games/";
    games.append(dir);
    if (auto p = vfs.resolve(games, prefixed); p && vfs.dir_exists(*p))
        return *p;
    return games;
}

struct IsFileItem {
    using Result = bool;

    [[nodiscard]] bool on(const MglItem::Load& load, const MglItem::Head&) noexcept {
        return load.slot == proto::FileRowKind::File;
    }

    [[nodiscard]] bool on(const MglItem::Reset&, const MglItem::Head&) noexcept { return false; }

    [[nodiscard]] bool misrouted(const MglItem&) noexcept { return false; }
};

}

MglPump::MglPump(const svc::Vfs& vfs, const os::IClock& clock, Wiring wiring) noexcept
    : vfs_(vfs),
      identity_(wiring.identity),
      conf_cell_(wiring.conf),
      names_(wiring.names),
      clock_(clock),
      info_(wiring.info),
      asks_(wiring.asks),
      diag_(wiring.diag),
      player_(
          MglPlayer::Wiring{.link_tx = wiring.link_tx, .asks = wiring.asks, .names = wiring.names}
      ) {}

void MglPump::bind_homes() {
    const std::optional<SessionIdentity> id = identity_.copy();
    bool bound = id.has_value();
    if (id) {

        const std::string file_home = games_home(vfs_, home_name(*id));
        const std::string image_home = !id->cue_dir.empty() ? games_home(vfs_, id->cue_dir)
                                                            : file_home;
        bound = player_.set_homes(file_home, image_home);
    }
    if (bound)
        return;

    ++stats_.homes_unbound;
    diag_.appendf("{\"t\":\"ev\",\"k\":\"MglHomeUnbound\",\"n\":%u}", stats_.homes_unbound);
}

CoreScope MglPump::sample_scope() {
    if (conf_cell_.sample_into(conf_scratch_) == 0)
        return {};
    return CoreScope{conf_scratch_.gen};
}

void MglPump::latch_remember_() {
    if (remember_ == MglRemember::Never)
        return;

    if (player_.scope().gen.v == 0 || conf_scratch_.truncated)
        return;
    const auto table = proto::ItemTable::parse(conf_scratch_.view());
    if (!table)
        return;
    const std::optional<SessionIdentity> id = identity_.copy();
    const RememberedStem stem = id ? id->stem : RememberedStem::of(table->core_name(), {});
    const proto::FileSlots slots = proto::FileSlots::of(*table);
    SlotRemember remember{player_, slots, stem};
    for (MglItemNo i{}; i.below(player_.count()); i = i.next())
        infra::dispatch<MglItemRoutes>(player_.item(i), remember, i);
}

bool MglPump::take_playlist(std::string_view path) noexcept {
    TASTY_SEAT_BODY(MglPump);
    const PublishOnExit publish_on_exit{this};

    (void)latch_.assign(path);

    if (pending_)
        ++stats_.superseded;
    pending_ = true;
    latched_doc_.reset();
    ++stats_.routed;
    return true;
}

bool MglPump::take_parsed(const Mgl& doc) noexcept {
    TASTY_SEAT_BODY(MglPump);
    const PublishOnExit publish_on_exit{this};
    if (!doc.complete()) {
        ++stats_.read_failures;
        last_error_ = Errc::bad_format;
        return false;
    }
    latched_doc_ = doc;

    if (pending_)
        ++stats_.superseded;
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

    if (!load_tag_ || tag != load_tag_)
        return;
    load_tag_ = kUncaused;
    if (state_ != State::AwaitingCore)
        return;

    if (!mgl_capable) {
        abandon(InfoId::None, Errc::negotiation);
        return;
    }

    if (!armed_doc_) {
        player_.parse(std::string_view{doc_, doc_len_}, true);
    } else {

        const Mgl& mgl = *armed_doc_;
        player_.adopt(
            infra::make<MglItem>(
                MglItem::Load{.slot = mgl.slot, .index = mgl.index, .path = mgl.path},
                MglItem::Head{.delay_s = mgl.delay_s}
            ),
            MglPlayer::PathBase::Card,
            true
        );
    }
    armed_doc_.reset();
    bind_homes();
    player_.set_scope(sample_scope());
    latch_remember_();
    if (player_.count() == 0) {

        enter(State::Idle);
        return;
    }

    IsFileItem is_file;
    for (MglItemNo i{}; i.below(player_.count()); i = i.next())
        if (infra::dispatch<MglItemRoutes>(player_.item(i), is_file))
            ++stats_.file_items;
    player_.arm(clock_);
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
    if (!load_tag_ || tag != load_tag_)
        return;
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

    if (state_ != State::AwaitingCore)
        return;
    if (!load_tag_ || tag != load_tag_)
        return;
    load_tag_ = kUncaused;

    if (why == Errc::cancelled) {
        abandon(InfoId::None, Errc::cancelled);
        return;
    }
    ++stats_.load_retries;
    if (retries_used_ >= kMaxLoadRetries || retry_deadline_.expired(clock_)) {
        abandon(InfoId::CoreLoadFailed, Errc::negotiation);
        return;
    }
    ++retries_used_;
    enter(State::PendingRead);
}

void MglPump::replay_busy_(CorrelationTag tag) {
    const bool first = !busy_since_;
    if (first)
        busy_since_ = os::Deadline::in(clock_, std::chrono::milliseconds{kBusyBoundMs});
    if (!first && busy_since_->expired(clock_)) {
        if (!player_.owns(tag))
            return;
        if (state_ == State::Idle)
            ++stats_.abandoned;
        abandon(InfoId::CoreLoadFailed, Errc::timeout);
        return;
    }
    if (!player_.replay_refused(tag, clock_))
        return;
    ++stats_.busy_replays;
    state_ = State::Playing;
    check_invariant();
}

void MglPump::enter(State s) noexcept {

    if (s != State::Playing)
        player_.reset();
    state_ = s;
    check_invariant();
}

void MglPump::check_invariant() const noexcept {
#ifndef NDEBUG
    if ((state_ == State::Playing) != !player_.done()) {
        fatal(
            Error{Errc::negotiation, ERR_SITE(), static_cast<std::uint32_t>(state_)},
            "MglPump: player_ non-Done and state_ == Playing must agree"
        );
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

            if (retry_deadline_.expired(clock_)) {
                abandon(InfoId::CoreLoadFailed, Errc::negotiation);
                return;
            }
            (void)publish_load();
            return;

        case State::AwaitingCore:

            if (arm_deadline_.expired(clock_)) {
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
    const std::string_view path = latch_.view();

    if (state_ != State::Idle)
        ++stats_.superseded;
    enter(State::Idle);

    load_tag_ = kUncaused;
    retries_used_ = 0;
    doc_len_ = 0;
    rbf_.clear();
    want_.clear();

    armed_doc_ = std::move(latched_doc_);
    latched_doc_.reset();
    if (armed_doc_) {
        if (!resolve_and_load(armed_doc_->rbf.view()))
            armed_doc_.reset();
        return;
    }

    if (path.empty()) {
        fail_read_(Errc::bad_format);
        return;
    }

    auto f = vfs_.open(path, svc::OpenMode::ReadWhole);
    if (!f) {
        fail_read_(f.error().code);
        return;
    }
    auto sz = (*f)->size();
    if (!sz || sz->v == 0 || sz->v > kMglDocMax) {

        fail_read_(sz ? Errc::bad_format : sz.error().code);
        return;
    }
    auto got = (*f)->read_at(
        0,
        std::as_writable_bytes(std::span(doc_)).first(static_cast<std::size_t>(sz->v))
    );
    if (!got || *got == 0) {
        fail_read_(got ? Errc::io : got.error().code);
        return;
    }
    doc_len_ = static_cast<std::uint32_t>(*got);

    (void)resolve_and_load(xml::rbf_text(std::string_view{doc_, doc_len_}));
}

bool MglPump::resolve_and_load(std::string_view want) {
    if (want.empty()) {

        ++stats_.no_rbf;
        last_error_ = Errc::not_found;
        if (info_)
            info_->info(InfoId::CoreNotFound);
        last_info_ = InfoId::CoreNotFound;
        doc_len_ = 0;
        return false;
    }
    (void)want_.assign(want);

    if (auto r = resolve_rbf(want_.view()); !r) {
        ++stats_.no_rbf;
        last_error_ = r.error().code;
        if (info_)
            info_->info(InfoId::CoreNotFound);
        last_info_ = InfoId::CoreNotFound;
        doc_len_ = 0;
        return false;
    }

    enter(State::PendingRead);
    retry_deadline_ = os::Deadline::in(clock_, std::chrono::milliseconds{kLoadRetryMs});
    (void)publish_load();
    return true;
}

bool MglPump::publish_load() {
    UiRequest::LoadCore req{};

    req.xml = XmlKind::Rbf;
    if (!req.path.assign(rbf_.view())) {
        abandon(InfoId::CoreLoadFailed, Errc::bad_format);
        return false;
    }

    const CorrelationTag tag = asks_.push(req);
    if (!tag) {
        ++stats_.load_retries;
        return false;
    }
    ++stats_.loads;
    load_tag_ = tag;
    enter(State::AwaitingCore);
    arm_deadline_ = os::Deadline::in(clock_, std::chrono::milliseconds{kArmTimeoutMs});
    return true;
}

void MglPump::pump_advance() {

    for (unsigned i = 0; i < kAdvanceCap; ++i) {
        const MglState before = player_.state();
        const MglItemNo before_item = player_.current();
        const std::uint32_t before_pub = player_.publishes();
        const std::uint32_t before_drop = player_.drops();
        const std::uint32_t before_saved = player_.remembered();
        const std::uint32_t before_unsaved = player_.remember_failures();
        auto r = player_.advance(clock_);
        stats_.publishes += player_.publishes() - before_pub;
        stats_.item_drops += player_.drops() - before_drop;
        stats_.paths_remembered += player_.remembered() - before_saved;
        stats_.remember_failures += player_.remember_failures() - before_unsaved;
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
    armed_doc_.reset();
    last_error_ = code;
    if (id != InfoId::None) {
        last_info_ = id;
        if (info_)
            info_->info(id);
    }

    diag_.appendf(
        "{\"t\":\"ev\",\"k\":\"MglAbandoned\","
        "\"err\":%u,\"info\":%u,\"ab\":%u,\"fi\":%u}",
        static_cast<unsigned>(code),
        static_cast<unsigned>(id),
        stats_.abandoned,
        stats_.file_items
    );
}

void MglPump::fail_read_(Errc code) noexcept {
    ++stats_.read_failures;
    last_error_ = code;
    if (info_)
        info_->info(InfoId::CoreLoadFailed);
    last_info_ = InfoId::CoreLoadFailed;
}

Ex<void> MglPump::resolve_rbf(std::string_view want) {
    const auto [dir, stem] = split_rbf(want);

    auto found = resolve_rbf_name(vfs_, dir, stem, false);
    if (!found)
        return std::unexpected(found.error());
    const std::string_view best = *found;

    rbf_.clear();
    if (!dir.empty()) {
        (void)rbf_.assign(dir);
        (void)rbf_.append("/");
    }
    (void)rbf_.append(best);
    return {};
}

}
