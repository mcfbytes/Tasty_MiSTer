// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <zlib.h>

#include "app/frame_arena.h"
#include "app/frame_codec.h"
#include "app/picture_size.h"
#include "app/rec_options.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"

namespace mister::app {

struct ZmbvLayout {

    static constexpr std::size_t kPixelBytes = 4;

    static constexpr std::size_t kMvBytesPerBlock = 2;

    static constexpr std::size_t kMvAlign = 4;
    static constexpr std::size_t kMvAlignMask = kMvAlign - 1;

    static constexpr std::size_t kInterHeaderBytes = 1;
    static constexpr std::size_t kKeyHeaderBytes = 7;
    static constexpr std::uint8_t kKeyframeFlag = 1;
    static constexpr std::uint8_t kVersionMajor = 0;
    static constexpr std::uint8_t kVersionMinor = 1;
    static constexpr std::uint8_t kCompressionZlib = 1;
    static constexpr std::uint8_t kFormatTopDownBgr0 = 8;
};

class ZmbvCodec final : public IFrameCodec {
    TASTY_SEAT_RESIDENT(Encode);

public:
    static constexpr bool kAvailable = true;
    static constexpr int kBlock = 16;
    static constexpr int kMeRange = 8;
    static constexpr int kMeClockEvery = 4;
    static constexpr int kMeWidenPct = 40;
    static constexpr int kMeNarrowPct = 60;
    static constexpr int kMeWidenRun = 8;
    static constexpr std::int64_t kMeSearchFloorNs = 500'000;

    static constexpr std::size_t kSmallCands = 16;
    static constexpr int kLevel = 1;
    static_assert(kMeWidenPct > 0 && kMeWidenPct < kMeNarrowPct && kMeNarrowPct < 100);
    static_assert(kMeWidenRun > 1);
    static_assert(kMeSearchFloorNs > 0);

    ZmbvCodec() noexcept = default;
    ~ZmbvCodec() override;
    ZmbvCodec(const ZmbvCodec&) = delete;
    ZmbvCodec& operator=(const ZmbvCodec&) = delete;

    void end() noexcept override;

    [[nodiscard]] PictureSize size() const noexcept override {
        TASTY_SEAT_BODY(ZmbvCodec);
        return size_;
    }

    [[nodiscard]] std::size_t max_payload() const noexcept override {
        TASTY_SEAT_BODY(ZmbvCodec);
        return max_payload_;
    }

    [[nodiscard]] std::size_t mapped_bytes() const noexcept { return arena_.bytes(); }

    [[nodiscard]] Encoded encode(
        RgbRows rgb,
        bool key,
        std::span<std::byte> out,
        const FrameBudget& budget
    ) noexcept override;
    [[nodiscard]] std::span<const std::byte> rekey(std::span<std::byte> out) noexcept override;
    [[nodiscard]] std::span<const std::byte> dup() noexcept override;

    [[nodiscard]] Search restart(const RecOptions& opt) noexcept override;

    [[nodiscard]] Search search() const noexcept {
        TASTY_SEAT_BODY(ZmbvCodec);
        return {.radius = static_cast<std::uint8_t>(rung_ ? rung_->radius() : 0), .cut = me_cut_};
    }

    [[nodiscard]] std::uint32_t sad_calls() const noexcept { return sad_calls_; }

private:
    struct MeRung {
        std::size_t v = 0;

        [[nodiscard]] static constexpr MeRung first() noexcept { return MeRung{}; }

        [[nodiscard]] static MeRung full() noexcept;

        [[nodiscard]] constexpr bool short_list() const noexcept { return v == 0; }

        [[nodiscard]] int radius() const noexcept;

        [[nodiscard]] MeRung wider() const noexcept;
        [[nodiscard]] MeRung narrower() const noexcept;
        [[nodiscard]] constexpr bool operator==(const MeRung&) const noexcept = default;
    };

    void release_() noexcept;
    [[nodiscard]] Ex<void> begin_(PictureSize size) noexcept override;
    void arm_(const FrameBudget& budget) noexcept;

    [[nodiscard]] std::span<const std::byte> deflate_sync_(
        std::span<const std::byte> in,
        std::span<std::byte> dst
    ) noexcept;
    [[nodiscard]] std::span<const std::byte> emit_key_(
        std::span<const std::byte> bgr0,
        std::span<std::byte> out
    ) noexcept;
    [[nodiscard]] std::span<const std::byte> emit_inter_(
        std::span<const std::byte> plain,
        std::span<std::byte> out
    ) noexcept;
    [[nodiscard]] std::span<const std::byte> build_inter_() noexcept;
    void to_bgr0_(RgbRows rgb) noexcept;
    void note_post_(std::int64_t post) noexcept;
    void note_(std::int64_t spent, bool cut) noexcept;

    FrameArena arena_{};
    z_stream z_{};
    std::span<std::byte> prev_{};
    std::span<std::byte> cur_{};
    std::span<std::byte> plain_{};
    std::span<std::byte> pkt_{};
    std::span<std::byte> dup_plain_{};
    std::span<std::byte> hist_{};
    infra::OptRef<const os::IClock> clock_{};
    std::int64_t threshold_ns_ = 0;
    std::int64_t search_budget_ns_ = 0;
    std::int64_t frame_t0_ = 0;
    std::int64_t post_ns_ = 0;
    std::size_t dup_plain_len_ = 0;
    std::size_t max_payload_ = 0;
    PictureSize size_{};
    std::uint32_t me_cut_ = 0;
    std::uint32_t sad_calls_ = 0;

    std::optional<MeRung> rung_ = MeRung{};
    int widen_run_ = 0;
    RecMotion motion_ = RecMotion::Auto;
    bool z_ready_ = false;
    bool post_set_ = false;
    bool cut_frame_ = false;
};

}
