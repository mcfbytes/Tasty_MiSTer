// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/cd_drives.h"

#include <algorithm>
#include <array>

#include "cores/minimig/cd_bus.h"
#include "cores/minimig/cd_decl.h"
#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/message_sum.h"
#include "infra/span_cut.h"

namespace mister::cores::minimig {

namespace {

bool is_none(const proto::MailboxAct& act) noexcept {
    return infra::kind_of(act) == proto::MailboxAct::Kind::None;
}

}

void CdDrives::set_active(CdSlot slot, bool on) noexcept {
    TASTY_SEAT_BODY(CdDrives);
    switch (slot) {
        case CdSlot::Cd32:
            akiko_.set_active(on);
            return;
        case CdSlot::Cdtv:
            cdtv_.set_active(on);
            return;
    }
}

void CdDrives::bind(CdSlot slot, std::optional<CdDisc> disc, std::string_view path) noexcept {
    TASTY_SEAT_BODY(CdDrives);
    switch (slot) {
        case CdSlot::Cd32:
            akiko_.bind(std::move(disc), path);
            return;
        case CdSlot::Cdtv:
            cdtv_.bind(std::move(disc), path, clock_.now());
            return;
    }
}

void CdDrives::reset() noexcept {
    TASTY_SEAT_BODY(CdDrives);
    akiko_.init();
    cdtv_.init(clock_.now());
}

void CdDrives::on_osd(bool shown) noexcept {
    TASTY_SEAT_BODY(CdDrives);
    akiko_.on_osd(shown);
}

void CdDrives::begin_core() noexcept {
    TASTY_SEAT_BODY(CdDrives);
    akiko_.forget();
    cdtv_.forget(clock_.now());
    out_ = Out::None;
    status_fresh_ = false;
}

void CdDrives::rebind(std::string_view, bool) noexcept {
    TASTY_SEAT_BODY(CdDrives);
    out_ = Out::None;
    status_fresh_ = false;
}

void CdDrives::route_(
    proto::MailboxSelectIndex select,
    const proto::user_io::PollFrame& frame
) noexcept {
    if (frame.kept == proto::ReplyCount{})
        return;
    if (select == cd_decl::kStatusSelect) {
        const CdStatus status{frame.reply(cd_decl::kFirstReply)};
        akiko_.on_status(status);
        cdtv_.on_status(status);
        status_fresh_ = true;
        return;
    }
    if (select == cd_decl::kSectorCounterSelect) {
        akiko_.on_sector_counter(akiko::SectorCounter::of(frame.replies[cd_decl::kFirstReply]));
        return;
    }
    std::array<std::byte, proto::user_io::kMaxPollFrameReplies.size()> bytes{};
    const infra::ByteCount n = frame.low_bytes(bytes);
    if (select == cd_decl::kAkikoDrainSelect) {
        akiko_.on_command(infra::affine::first(std::span{bytes}, n));
        return;
    }
    if (select == cd_decl::kCdtvDrainSelect) {
        for (const std::byte b : infra::affine::first(std::span{bytes}, n))
            cdtv_.on_byte(b);
    }
}

proto::MailboxAct CdDrives::serve(
    proto::MailboxSelectIndex select,
    const proto::user_io::PollFrame& frame,
    ActBytes,
    BulkBytes bulk
) noexcept {
    TASTY_SEAT_BODY(CdDrives);
    route_(select, frame);
    return pump_(bulk);
}

proto::MailboxAct CdDrives::settle(
    const proto::MailboxResult& result,
    ActBytes,
    BulkBytes bulk
) noexcept {
    TASTY_SEAT_BODY(CdDrives);
    switch (std::exchange(out_, Out::None)) {
        case Out::Akiko:
            akiko_.settled(result, bulk);
            status_fresh_ = false;
            return {};
        case Out::Cdtv:
            cdtv_.settled(result, bulk);
            status_fresh_ = false;
            return {};
        case Out::Tick:
            return pump_(bulk);
        case Out::None:
            return {};
    }
    return {};
}

proto::MailboxAct CdDrives::pump_(BulkBytes bulk) noexcept {
    if (out_ != Out::None || !status_fresh_)
        return {};
    const Instant now = clock_.now();
    const bool cdtv_first = last_ == Out::Akiko;

    const std::array<Out, 2> order = cdtv_first ? std::array<Out, 2>{Out::Cdtv, Out::Akiko}
                                                : std::array<Out, 2>{Out::Akiko, Out::Cdtv};
    for (const Out turn : order) {
        proto::MailboxAct act = turn == Out::Cdtv ? cdtv_.next(now, bulk) : akiko_.next(now, bulk);
        if (is_none(act))
            continue;
        out_ = last_ = turn;
        return act;
    }
    return tick_(now);
}

proto::MailboxAct CdDrives::tick_(Instant now) noexcept {
    std::optional<Instant> at = akiko_.deadline(now);
    if (const std::optional<Instant> c = cdtv_.deadline(now))
        at = at ? std::min(*at, *c) : c;
    if (!at)
        return {};
    out_ = Out::Tick;
    return cd_bus::wait_until(std::clamp(*at, now + kTickFloor, now + kTickCap));
}

}
