// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "cores/atari8/sio_checksum.h"
#include "cores/atari8/sio_frame.h"
#include "cores/atari8/sio_types.h"
#include "infra/affine.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"

namespace mister::cores::atari8 {

class SioFrameReader {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    enum class Fed : std::uint8_t { More, Dropped, Complete };

    static constexpr SioWordCount kTagged{5};
    static constexpr SioWordCount kWords{6};

    static constexpr SioFramePlace kDevice{0};
    static constexpr SioFramePlace kCode{1};
    static constexpr SioFramePlace kAux1{2};
    static constexpr SioFramePlace kChecksum{4};
    static constexpr SioWordCount kAuxBytes{2};

    [[nodiscard]] SioWordCount wanted() const noexcept {
        TASTY_SEAT_BODY(SioFrameReader);
        return kWords - (at_ - SioFramePlace{});
    }

    [[nodiscard]] Fed feed(SioRxWord word, SioError error) noexcept {
        TASTY_SEAT_BODY(SioFrameReader);
        if (infra::affine::in_bounds(kTagged, at_)) {
            const SioError seen = error.with(carried_);
            carried_ = {};
            if (seen.any() || word.place() != at_) {
                at_ = SioFramePlace{};
                return Fed::Dropped;
            }
            bytes_[at_] = word.byte();
            at_ += SioWordCount{1};
            return Fed::More;
        }

        carried_ = carried_.with(error);
        at_ = SioFramePlace{};
        const std::span<const std::byte> summed = bytes_.prefix(kTagged - SioWordCount{1});
        if (sio_checksum(summed) != bytes_[kChecksum])
            return Fed::Dropped;
        frame_ = SioFrame{
            .device = SioDevice{std::to_integer<std::uint8_t>(bytes_[kDevice])},
            .code = bytes_[kCode],
            .sector = SioSector::load_le(bytes_.prefix(kTagged).subspan<kAux1.v, kAuxBytes.v>())
        };
        return Fed::Complete;
    }

    [[nodiscard]] const SioFrame& frame() const noexcept {
        TASTY_SEAT_BODY(SioFrameReader);
        return frame_;
    }

    void reset() noexcept {
        TASTY_SEAT_BODY(SioFrameReader);
        at_ = {};
        carried_ = {};
    }

private:
    SioFramePlace at_{};
    infra::IndexedArray<std::byte, 5, SioFramePlace> bytes_{};
    SioError carried_{};
    SioFrame frame_{};
};

}
