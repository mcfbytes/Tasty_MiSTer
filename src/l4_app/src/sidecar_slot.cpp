// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sidecar_slot.h"

#include <utility>

#include "cores/payload_pieces.h"
#include "cores/registry.h"
#include "cores/sidecar_ask.h"
#include "infra/prelude.h"

namespace mister::app {

void SidecarSlot::make(const NegotiatedCore& negotiated) {
    TASTY_SEAT_BODY(SidecarSlot);
    withdraw();
    const auto hint = negotiated.made_with_manifest() ? cores::LoadHint::XmlManifest
                                                      : cores::LoadHint::None;
    const auto row = cores::find_core(negotiated.core_name().view(), hint);
    if (!row || row->make_sidecar == nullptr)
        return;
    builder_ = row->make_sidecar();
}

void SidecarSlot::withdraw() noexcept {
    TASTY_SEAT_BODY(SidecarSlot);
    send_.reset();
    builder_.reset();
}

void SidecarSlot::cancel() noexcept {
    TASTY_SEAT_BODY(SidecarSlot);
    send_.reset();
}

bool SidecarSlot::plan(std::string_view pick, proto::WideIoIndex index, const svc::Vfs& vfs) {
    TASTY_SEAT_BODY(SidecarSlot);
    if (builder_ == nullptr)
        return false;
    cores::SidecarPlan plan = builder_->plan(cores::SidecarAsk{.pick = pick, .index = index}, vfs);
    if (plan.empty())
        return false;
    if (send_) {
        send_->append(std::move(plan));
    } else {
        send_.emplace(std::move(plan));
    }
    return true;
}

bool SidecarSlot::wants_pass(const LinkTxChannel<SeatTag::Main>& inbox) const noexcept {
    TASTY_SEAT_BODY(SidecarSlot);
    return send_.has_value() && inbox.ring().size() < cores::PayloadPieces::kAhead;
}

void SidecarSlot::step(cores::ILadderHost& host, OwnerTally& tally) {
    TASTY_SEAT_BODY(SidecarSlot);
    if (!send_ || send_->step(host) == cores::SidecarSend::Pass::Waiting)
        return;
    tally.sidecars_sent.add(send_->sent());
    tally.sidecar_failures.add(send_->failures());
    send_.reset();
}

}
