// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/link_binder.h"

#include "app/link_rows.h"
#include "app/pcm/pcm_ring_feeder.h"

#include <optional>
#include <utility>

#include "cores/core.h"
#include "infra/prelude.h"

namespace mister::app {
LinkBinder::LinkBinder(hal::ISpiTransport& link, const Wiring& w) noexcept
    : fio_queue_(link, proto::link::SpiFioQueue::kPieceWords, w.tenant),
      spi_sink_(link, infra::OptRef<proto::link::SpiFioQueue>{fio_queue_}),
      feeder_(w.feeder),
      exec_(w.exec),
      state_(w.state) {}

proto::IImageSink& LinkBinder::grant_bulk() noexcept {
    return progress_sink_;
}

void LinkBinder::release_feeder() noexcept {
    if (feeder_granted_ && feeder_)
        feeder_->detach();
    feeder_granted_ = false;
}

bool LinkBinder::adopt_fed_ring(hal::FpgaMemory&& ring) noexcept {

    if (!feeder_ || !feeder_->adopt_ring(std::move(ring))) {
        ++fed_refusals_;
        return false;
    }
    feeder_granted_ = true;
    return true;
}

Ex<void> LinkBinder::bind_census(
    std::span<const reactor::LinkDecoderDecl> rows,
    infra::OptRef<cores::Core> owner
) {

    const infra::OptRef<reactor::ICoreToken> prev = state_.owner;
    state_.owner = (rows.empty() || !owner) ? infra::OptRef<reactor::ICoreToken>{}
                                            : infra::OptRef<reactor::ICoreToken>{*owner};
    auto r = exec_.bind(rows, state_);
    if (!r)
        state_.owner = prev;
    return r;
}

Ex<void> LinkBinder::bind_session(
    std::span<const reactor::LinkDecoderDecl> rows,
    infra::OptRef<cores::Core> owner
) {
    for (const LinkRow& row : kLinkRows) {
        if (row.phase != BindPhase::Bind)
            continue;
        switch (row.rt) {
            case RtAct::BindCensus: {
                if (auto r = bind_census(rows, owner); !r)
                    return r;
                break;
            }
            case RtAct::None:
                break;
            case RtAct::kCount:
                break;
        }
    }
    return {};
}

bool LinkBinder::bridge_windows_live() const noexcept {

    if (feeder_ && feeder_->attached())
        return true;
    return feeder_granted_;
}

Ex<void> LinkBinder::unbind_session() {
    return bind_census({}, {});
}

}
