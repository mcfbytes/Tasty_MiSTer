// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ocram_host_enable.h"

#include <cstdio>
#include <utility>

#include "infra/prelude.h"

namespace mister::app {

namespace ocram = hal::ocram;

namespace {

constexpr ocram::ImageRowBytes kOsdRowBytes = ocram::ImageRowBytes::of_words(
    ocram::RecordHeader::kRowWords
);

void say(const char* what) noexcept {
    std::fprintf(stderr, "mister: OCRAM: %s\n", what);
}
}

OcramHostEnable::OcramHostEnable(
    BayCounts& counts,
    BayCountsCell& cell,
    const os::IClock& clock,
    Wiring wiring
) noexcept
    : counts_(counts),
      cell_(cell),
      clock_(clock),
      delay_(wiring.delay),
      window_(wiring.window),
      platform_(wiring.platform),
      prestaged_(wiring.mapped) {}

std::optional<ocram::Nonce> OcramHostEnable::prepare(
    hal::OcramPresence presence,
    FabricGeneration fabric
) noexcept {
    TASTY_SEAT_BODY(OcramHostEnable);

    session_.reset();
    bound_seen_ = false;
    lost_ = false;
    if (!presence.present()) {
        say("GPI[26] reads 0: no OCRAM master in this bitstream (normal), no hello");
        return std::nullopt;
    }
    if (window_.len == 0 || !platform_) {
        say("the board row has no mailbox (normal for it), no hello");
        return std::nullopt;
    }

    if (fabric != fabric_)
        run_edge_ = false;
    fabric_ = fabric;

    if (no_edge_) {
        if (fabric == latched_at_) {
            say("a missed stop proof latched this configuration, no hello");
            counts_.ocram_refusals.add();
            publish_();
            return std::nullopt;
        }
        no_edge_ = false;
    }

    if (!platform_->f2h_bridge_released()) {
        say("the fpga2hps bridge is held in reset, no RUN edge");
        counts_.ocram_refusals.add();
        publish_();
        return std::nullopt;
    }
    if (!mapped_) {
        if (prestaged_) {
            mapped_.emplace(std::move(*prestaged_));
            prestaged_.reset();
        } else {
            auto m = ocram::Window::map(window_);
            if (!m) {
                std::fprintf(
                    stderr,
                    "mister: OCRAM: window \"%s\" (%zu B) refused (detail=%u), no hello\n",
                    window_.name,
                    window_.len,
                    m.error().detail
                );
                counts_.ocram_refusals.add();
                publish_();
                return std::nullopt;
            }
            mapped_.emplace(std::move(*m));
            std::fprintf(
                stderr,
                "mister: OCRAM: window \"%s\" (%zu B) mapped\n",
                window_.name,
                window_.len
            );
        }
    }

    if (run_edge_ && !stop_proof_(kStopBound)) {
        say("the previous run's stop proof missed, no hello; latched");
        publish_();
        return std::nullopt;
    }

    platform_->disarm_warm_boot();
    platform_->clear_ecc_flags();
    mapped_->page().zero();
    nonce_ = nonce_.next();
    say("present: warm boot disarmed, page zeroed, raising RUN");
    publish_();
    return nonce_;
}

void OcramHostEnable::raised() noexcept {
    TASTY_SEAT_BODY(OcramHostEnable);
    run_edge_ = true;
    counts_.ocram_hellos.add();
    publish_();
}

bool OcramHostEnable::handshake(ocram::Nonce nonce) noexcept {
    TASTY_SEAT_BODY(OcramHostEnable);
    if (!mapped_)
        return false;
    if (!poll_hello(ocram::HelloState::armed(), kArmBound)) {
        say("no armed C_HELLO within kArmBound, hello dropped");
        counts_.ocram_refusals.add();
        publish_();
        return false;
    }
    counts_.ocram_armed.add();
    ocram::ControlPage page = mapped_->page();
    page.store_h_hello(ocram::HelloState::session(nonce));
    if (!poll_hello(ocram::HelloState::session(nonce), kEchoBound)) {
        say("no echo of H_HELLO within kEchoBound, hello dropped");
        counts_.ocram_refusals.add();
        publish_();
        return false;
    }
    counts_.ocram_echoes.add();
    publish_();
    return true;
}

std::optional<OcramHostEnable::Session> OcramHostEnable::read_session() noexcept {
    TASTY_SEAT_BODY(OcramHostEnable);
    if (!mapped_)
        return std::nullopt;
    const ocram::ControlPage page = mapped_->page();
    const auto no_ring = [this](const char* why) -> std::optional<Session> {
        std::fprintf(stderr, "mister: OCRAM: echoed, but no ring served (%s): LW or link\n", why);
        counts_.ocram_no_ring.add();
        publish_();
        return std::nullopt;
    };
    if (!page.c_version().layout_1())
        return no_ring("C_VERSION is not layout 1.0");
    Session got{.nonce = nonce_};
    const char* osd_why = osd_rings_(page, got);
    const char* sector_why = sector_rings_(page, got);
    const char* lane_why = lane_rings_(page, got);
    if (osd_why != nullptr)
        std::fprintf(stderr, "mister: OCRAM: no OSD ring (%s)\n", osd_why);
    if (sector_why != nullptr) {
        std::fprintf(stderr, "mister: OCRAM: no SECTOR ring (%s)\n", sector_why);
    }
    if (lane_why != nullptr) {
        std::fprintf(stderr, "mister: OCRAM: no download lane (%s)\n", lane_why);
    }
    if (!got.ring && !got.sector && !got.lane)
        return no_ring("no OSD, SECTOR or download tier");
    session_ = got;
    say("session up: the build carries the declared rings");
    counts_.ocram_sessions.add();
    publish_();
    return session_;
}

const char* OcramHostEnable::osd_rings_(const ocram::ControlPage& page, Session& got) noexcept {
    if (!page.c_caps().ring(ocram::Ring::Osd))
        return "C_CAPS declares none";
    const std::optional<ocram::RingDesc> ring = page.c_ring(ocram::Ring::Osd);
    if (!ring || ring->kind() != ocram::RingDesc::Kind::Osd)
        return "C_RING malformed";
    if (page.faulted(ocram::Ring::Osd))
        return "C_STATE faulted";
    const std::optional<ocram::ImageRows> rows = page.c_k_osd_rows();
    const std::optional<ocram::ImageRowBytes> bytes = page.c_k_osd_row_bytes();

    if (!rows || rows->empty() || !bytes || *bytes != kOsdRowBytes)
        return "C_K shape";
    got.ring = *ring;
    got.rows = *rows;
    got.row_bytes = *bytes;
    return nullptr;
}

const char* OcramHostEnable::sector_rings_(const ocram::ControlPage& page, Session& got) noexcept {
    const ocram::OcramCaps caps = page.c_caps();
    if (!caps.ring(ocram::Ring::Sector) || !caps.ring(ocram::Ring::Evt)) {
        return "C_CAPS declares no SECTOR and EVT pair";
    }
    const std::optional<ocram::RingDesc> ring = page.c_ring(ocram::Ring::Sector);
    if (!ring || ring->kind() != ocram::RingDesc::Kind::Sector)
        return "C_RING[4] malformed";
    const std::optional<ocram::RingDesc> evt = page.c_ring(ocram::Ring::Evt);
    if (!evt || evt->kind() != ocram::RingDesc::Kind::Evt || !evt->line()) {
        return "C_RING[3] is no EVT ring with a line";
    }
    if (page.faulted(ocram::Ring::Sector) || page.faulted(ocram::Ring::Evt)) {
        return "C_STATE faulted";
    }
    const std::optional<ocram::SectorShape> shape = page.c_k_sector();
    if (!shape)
        return "C_K's slot words";
    SectorRings rings{
        .ring = *ring,
        .evt = *evt,
        .shape = *shape,
        .bind_records = caps.bind_record(),
        .events = caps.events()
    };
    if (const char* why = mount_rings_(page, rings); why != nullptr)
        std::fprintf(stderr, "mister: OCRAM: no mount ring (%s): the slots stay on SPI\n", why);
    got.sector = rings;
    return nullptr;
}

const char* OcramHostEnable::mount_rings_(
    const ocram::ControlPage& page,
    SectorRings& got
) noexcept {
    if (!page.c_caps().ring(ocram::Ring::Pad))
        return "C_CAPS declares no ring 0";
    const std::optional<ocram::RingDesc> ring = page.c_ring(ocram::Ring::Pad);
    if (!ring || ring->kind() != ocram::RingDesc::Kind::Sector)
        return "C_RING[0] malformed";
    if (page.faulted(ocram::Ring::Pad))
        return "C_STATE faulted";
    MountRings mounts{.ring = *ring};
    bool any = false;
    for (std::size_t i = 0; i < ocram::kMountSlots.size(); ++i) {
        mounts.slots[i] = page.c_k_mount(ocram::kMountSlots[i]);
        any = any || mounts.slots[i].has_value();
    }
    if (!any)
        return "C_K[10, 11] declare no slot";
    got.mounts = mounts;
    return nullptr;
}

const char* OcramHostEnable::lane_rings_(const ocram::ControlPage& page, Session& got) noexcept {
    if (!page.c_caps().ring(ocram::Ring::Lane))
        return "C_CAPS declares none";
    const std::optional<ocram::RingDesc> ring = page.c_ring(ocram::Ring::Lane);
    if (!ring || ring->kind() != ocram::RingDesc::Kind::Lane)
        return "C_RING[1] malformed";
    if (page.faulted(ocram::Ring::Lane))
        return "C_STATE faulted";
    const std::optional<ocram::LaneShape> shape = page.c_k_lane();
    if (!shape)
        return "C_K[9] names no FPGA select";
    got.lane = *ring;
    got.lane_shape = shape;
    return nullptr;
}

bool OcramHostEnable::stop_after_drop() noexcept {
    TASTY_SEAT_BODY(OcramHostEnable);
    counts_.ocram_drops.add();
    if (!mapped_) {
        run_edge_ = false;
        publish_();
        return true;
    }
    if (stop_proof_(kArmBound)) {
        say("RUN dropped, the master read stopped");
        publish_();
        return true;
    }
    say("RUN dropped, but the stop proof missed; latched");
    publish_();
    return false;
}

std::optional<proto::OsdRowCount> OcramHostEnable::osd_rows() const noexcept {
    if (!session_)
        return std::nullopt;
    return proto::OsdRowCount::from(session_->rows);
}

std::optional<ocram::RingLend> OcramHostEnable::lend_osd() const noexcept {
    if (!mapped_ || !session_ || !session_->ring)
        return std::nullopt;
    return mapped_->lend_ring(*session_->ring, session_->nonce);
}

std::optional<ocram::RingLend> OcramHostEnable::lend_lane() const noexcept {
    if (!mapped_ || !session_ || !session_->lane)
        return std::nullopt;
    return mapped_->lend_ring(*session_->lane, session_->nonce);
}

std::optional<OcramHostEnable::SectorRings> OcramHostEnable::sector_rings() const noexcept {
    if (!session_)
        return std::nullopt;
    return session_->sector;
}

std::optional<OcramHostEnable::SectorLends> OcramHostEnable::lend_sector() const noexcept {
    if (!mapped_ || !session_ || !session_->sector)
        return std::nullopt;
    SectorLends lends{
        .ring = mapped_->lend_ring(session_->sector->ring, session_->nonce),
        .evt = mapped_->lend_ring(session_->sector->evt, session_->nonce)
    };
    if (!lends.ring.lent() || !lends.evt.lent())
        return std::nullopt;

    if (session_->sector->mounts)
        lends.mount = mapped_->lend_ring(session_->sector->mounts->ring, session_->nonce);
    return lends;
}

void OcramHostEnable::bind_osd() noexcept {
    TASTY_SEAT_BODY(OcramHostEnable);
    if (!mapped_)
        return;
    const ocram::ControlPage page = mapped_->page();
    page.store_h_bind(page.h_bind().with(ocram::Ring::Osd));
}

void OcramHostEnable::bind_events() noexcept {
    TASTY_SEAT_BODY(OcramHostEnable);
    if (!mapped_ || !session_ || !session_->sector || !session_->sector->events)
        return;
    const ocram::ControlPage page = mapped_->page();
    page.store_h_bind(page.h_bind().with(ocram::Ring::Evt));
}

bool OcramHostEnable::session_lost() noexcept {
    TASTY_SEAT_BODY(OcramHostEnable);
    if (!mapped_ || !session_ || lost_)
        return lost_;
    const ocram::ControlPage page = mapped_->page();
    const bool echo = page.c_hello().echoes(ocram::HelloState::session(session_->nonce));

    const bool bind = !session_->ring || page.c_bind().ring(ocram::Ring::Osd);
    if (bind)
        bound_seen_ = true;

    const bool lane_stopped = session_->lane && page.faulted(ocram::Ring::Lane);

    if (echo && !lane_stopped && (bind || !bound_seen_))
        return false;
    if (!echo || lane_stopped || (bound_seen_ && !bind)) {
        say(!echo              ? "the echo went away: session lost"
                : lane_stopped ? "ring 1 stopped on a fault: session lost"
                               : "the OSD ring's bind went away: session lost");
        lost_ = true;
        counts_.ocram_lost.add();
        publish_();
    }
    return lost_;
}

void OcramHostEnable::proof() {
    TASTY_SEAT_BODY(OcramHostEnable);
    if (!mapped_ || !run_edge_)
        return;
    if (stop_proof_(kStopBound)) {
        publish_();
        return;
    }
    publish_();
}

void OcramHostEnable::drop() noexcept {
    TASTY_SEAT_BODY(OcramHostEnable);

    session_.reset();
    bound_seen_ = false;
    lost_ = false;
}

bool OcramHostEnable::stop_proof_(std::chrono::microseconds bound) noexcept {
    if (poll_hello(ocram::HelloState::stopped(), bound)) {
        run_edge_ = false;
        counts_.ocram_stopped.add();
        return true;
    }
    counts_.ocram_stop_misses.add();
    latch_();
    return false;
}

bool OcramHostEnable::poll_hello(ocram::HelloState want, std::chrono::microseconds bound) noexcept {
    const auto start = clock_.now();
    for (;;) {
        if (mapped_->page().c_hello() == want)
            return true;
        if (since_(start) >= bound)
            return false;
        delay_ref_().sleep_for(kPollGap);
    }
}

std::chrono::microseconds OcramHostEnable::since_(
    os::RawMonotonicClock::time_point start
) const noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(clock_.now() - start);
}

void OcramHostEnable::latch_() noexcept {
    no_edge_ = true;
    latched_at_ = fabric_;

    run_edge_ = false;
}

void OcramHostEnable::publish_() noexcept {
    cell_.publish(counts_);
}

}
