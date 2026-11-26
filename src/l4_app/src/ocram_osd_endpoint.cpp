// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ocram_osd_endpoint.h"

#include <array>
#include <cstddef>
#include <span>

#include "infra/endian.h"
#include "infra/prelude.h"
#include "infra/seat_owner_ui.h"

namespace mister::app {

namespace ocram = hal::ocram;

namespace {
constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
constexpr std::size_t kRowWords = proto::OsdSurface::kRowBytes / kWordBytes;
}

bool OcramOsdEndpoint::accepts_hook_(const OsdGrant& g) const noexcept {
    return g.body.ring.lent();
}

void OcramOsdEndpoint::publish_hook_(const OsdRoute& r) noexcept {
    w_.route.publish(r);
}

void OcramOsdEndpoint::prepare_hook_(const OsdGrant& g) noexcept {
    writer_.emplace(ocram::RingWriter<seat_owner::Ui>::redeem(g.body.ring));
}

bool OcramOsdEndpoint::arm_hook_(const OsdGrant&) noexcept {
    return true;
}

void OcramOsdEndpoint::bind_hook_(const OsdGrant& g) noexcept {
    image_rows_ = g.body.rows < kRows ? g.body.rows : kRows;
}

void OcramOsdEndpoint::bound_hook_() noexcept {
    tally_.bound = false;
    tally_.on_ocram = true;
    publish_();
}

void OcramOsdEndpoint::answer_hook_(proto::BuildGeneration) noexcept {}

void OcramOsdEndpoint::unbind_hook_() noexcept {
    tally_.on_ocram = false;
    writer_.reset();
    stored_ = {};
    image_rows_ = kRows;
}

void OcramOsdEndpoint::serve_bound_() noexcept {
    if (!writer_ || !writer_->lent())
        return;

    if (!writer_->echoed()) {
        tally_.ring_lost.add();
        publish_();
        unbind_();
        return;
    }

    if (!writer_->bound())
        return;
    const proto::OsdRowCount n = bound_rows_();
    for (proto::OsdRow r{}; n.covers(r); r += proto::kOneOsdRow) {
        if (w_.surface.seq(r) == stored_[r])
            continue;
        if (!publish_row_(r))
            break;
    }
}

bool OcramOsdEndpoint::idle_bound_hook_() const noexcept {
    return true;
}

bool OcramOsdEndpoint::publish_row_(proto::OsdRow r) noexcept {
    infra::CellGen g = w_.surface.seq(r);
    if (!g.held())
        return false;

    const infra::CellGen sampled{w_.surface.sample(r, row_)};
    if (!sampled.held())
        return false;
    g = sampled;
    const std::optional<ocram::RowArg> arg = r.record_arg();
    if (!arg || !writer_)
        return false;
    std::array<std::uint32_t, kRowWords> payload{};
    const std::span<const std::uint8_t> bytes{row_.b};
    for (std::size_t i = 0; i < kRowWords; ++i)
        payload[i] = infra::load_le<std::uint32_t>(
            bytes.subspan(kWordBytes * i).first<kWordBytes>()
        );
    if (!writer_->publish(ocram::RecordHeader::row(*arg), payload))
        return false;
    stored_[r] = g;
    ++tally_.rows;
    return true;
}

proto::OsdRowCount OcramOsdEndpoint::bound_rows_() const noexcept {
    const proto::OsdRowCount visible = w_.surface.visible_rows();
    return visible < image_rows_ ? visible : image_rows_;
}

template class GrantEndpoint<OsdGrant, OsdRoute, seat_owner::Ui>;

}

namespace mister::infra::xthread {
template class GrantLease<app::OsdGrant, seat_owner::Ui>;
}
