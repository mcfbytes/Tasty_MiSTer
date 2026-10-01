// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app/frame_arena.h"
#include "app/frame_codec.h"
#include "app/rec_options.h"
#include "infra/seat.h"
#include "os/clock.h"

namespace mister::app {

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
    static constexpr int kSmallCands = 16;
    static constexpr int kLevel = 1;
    static_assert(kMeWidenPct > 0 && kMeWidenPct < kMeNarrowPct && kMeNarrowPct < 100);
    static_assert(kMeWidenRun > 1);
    static_assert(kMeSearchFloorNs > 0);

    ZmbvCodec() noexcept = default;
    ~ZmbvCodec() override;
    ZmbvCodec(const ZmbvCodec&) = delete;
    ZmbvCodec& operator=(const ZmbvCodec&) = delete;

    [[nodiscard]] Ex<void> begin(std::uint16_t w, std::uint16_t h) noexcept override;
    void end() noexcept override;
    [[nodiscard]] bool open() const noexcept override {
        TASTY_SEAT_BODY(ZmbvCodec);
        return width_ != 0;
    }
    [[nodiscard]] std::uint16_t width() const noexcept override {
        TASTY_SEAT_BODY(ZmbvCodec);
        return width_;
    }
    [[nodiscard]] std::uint16_t height() const noexcept override {
        TASTY_SEAT_BODY(ZmbvCodec);
        return height_;
    }
    [[nodiscard]] std::size_t max_payload() const noexcept override {
        TASTY_SEAT_BODY(ZmbvCodec);
        return max_payload_;
    }
    [[nodiscard]] std::size_t mapped_bytes() const noexcept { return arena_.bytes(); }

    [[nodiscard]] std::size_t encode(const std::byte* rgb, std::size_t line, bool key,
                                     std::span<std::byte> out) noexcept override;
    [[nodiscard]] std::size_t rekey(std::span<std::byte> out) noexcept override;
    [[nodiscard]] std::span<const std::byte> dup() noexcept override;

    void restart(const RecOptions& opt) noexcept override;
    void arm(const os::IClock* clock, std::int64_t budget_ns) noexcept override;
    [[nodiscard]] Search search() const noexcept override {
        TASTY_SEAT_BODY(ZmbvCodec);
        return {.radius = static_cast<std::uint8_t>(radius_), .cut = me_cut_};
    }
    [[nodiscard]] std::uint32_t sad_calls() const noexcept { return sad_calls_; }

private:
    void release_() noexcept;
    [[nodiscard]] std::size_t deflate_sync_(const std::byte* in, std::size_t n, std::byte* dst,
                                            std::size_t cap) noexcept;
    [[nodiscard]] std::size_t emit_key_(const std::byte* bgr0, std::span<std::byte> out) noexcept;
    [[nodiscard]] std::size_t emit_inter_(const std::byte* plain, std::size_t n,
                                          std::span<std::byte> out) noexcept;
    [[nodiscard]] std::size_t build_inter_() noexcept;
    void to_bgr0_(const std::byte* rgb, std::size_t line) noexcept;
    void note_post_(std::int64_t post) noexcept;
    void note_(std::int64_t spent, bool cut) noexcept;
    void step_(int dir) noexcept;

    FrameArena arena_{};
    std::byte* prev_ = nullptr;
    std::byte* cur_ = nullptr;
    std::byte* plain_ = nullptr;
    std::byte* pkt_ = nullptr;
    std::byte* zmem_ = nullptr;
    std::byte* dup_plain_ = nullptr;
    std::int8_t* hist_ = nullptr;
    const os::IClock* clock_ = nullptr;
    std::int64_t threshold_ns_ = 0;
    std::int64_t search_budget_ns_ = 0;
    std::int64_t frame_t0_ = 0;
    std::int64_t post_ns_ = 0;
    std::size_t dup_plain_len_ = 0;
    std::size_t max_payload_ = 0;
    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
    std::uint32_t me_cut_ = 0;
    std::uint32_t sad_calls_ = 0;
    int bx_ = 0;
    int by_ = 0;
    int radius_ = 1;
    int widen_run_ = 0;
    RecMotion motion_ = RecMotion::Auto;
    bool z_ready_ = false;
    bool post_set_ = false;
    bool cut_frame_ = false;
};

}  // namespace mister::app
