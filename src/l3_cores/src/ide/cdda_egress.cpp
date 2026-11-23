// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/ide/cdda_egress.h"

#include "infra/span_cut.h"

#include <algorithm>

namespace mister::cores::ide {

bool CddaEgress::stage(AtapiDrive& drive) noexcept {
    TASTY_SEAT_BODY(CddaEgress);
    rest_ = {};
    if (!drive.play_frame(std::span{frame_}))
        return false;
    level_(drive.volumes());
    rest_ = frame_;
    return true;
}

std::optional<CddaEgress::Piece> CddaEgress::next() noexcept {
    TASTY_SEAT_BODY(CddaEgress);
    if (!pending())
        return std::nullopt;
    const std::span<const std::byte> bytes = infra::affine::first_clamped(rest_, kPiece);
    rest_ = rest_.subspan(bytes.size());
    return Piece{.bytes = bytes, .last = !pending()};
}

void CddaEgress::level_(AtapiDrive::Volumes volumes) noexcept {
    if (volumes == AtapiDrive::Volumes{})
        return;
    constexpr std::size_t kSample = kSampleBytes.size();

    for (std::span<std::byte> rest{frame_}; rest.size() >= kPairBytes.size();
         rest = rest.subspan(kPairBytes.size())) {
        volumes.port1.apply(rest.first<kSample>());
        volumes.port0.apply(rest.subspan<kSample, kSample>());
    }
}

}
