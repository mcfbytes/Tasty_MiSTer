// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "app/bracket_close.h"
#include "cores/load_progress.h"
#include "cores/progress_ticker.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/image_sink.h"
#include "proto/spi_image_sink.h"

namespace mister::app {

class ProgressImageSink final : public proto::IImageSink {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit ProgressImageSink(proto::SpiImageSink& inner) noexcept : inner_(&inner) {}

    [[nodiscard]] Ex<void> begin(proto::WideIoIndex index,
                                 const proto::SessionParams& params) override {
        TASTY_SEAT_BODY(ProgressImageSink);
        return inner_->begin(index, params);
    }
    [[nodiscard]] Ex<void> write(std::span<const std::uint8_t> data) override {
        TASTY_SEAT_BODY(ProgressImageSink);
        auto r = inner_->write(data);
        if (r) count(data.size());
        return r;
    }
    [[nodiscard]] Ex<void> end() override {
        TASTY_SEAT_BODY(ProgressImageSink);
        if (IBracketClose* const c = closing_; c != nullptr) {
            closing_ = nullptr;
            c->before_close();
        }
        return inner_->end();
    }

    void arm(cores::ILoadProgress& sink, std::uint64_t total,
             IBracketClose* closing = nullptr) noexcept {
        TASTY_SEAT_BODY(ProgressImageSink);
        sent_ = 0;
        armed_ = true;
        closing_ = closing;
        tick_.bind(&sink);
        tick_.begin(total);
    }

    void count(std::uint64_t n) noexcept {
        TASTY_SEAT_BODY(ProgressImageSink);
        if (!armed_) return;
        sent_ += n;
        tick_.advance(sent_);
    }
    void disarm() noexcept {
        TASTY_SEAT_BODY(ProgressImageSink);
        closing_ = nullptr;
        if (!armed_) return;
        armed_ = false;
        tick_.finish();
        tick_.bind(nullptr);
    }
    [[nodiscard]] bool armed() const noexcept { return armed_; }

private:
    proto::SpiImageSink* inner_;
    cores::ProgressTicker tick_{};
    IBracketClose* closing_ = nullptr;
    std::uint64_t sent_ = 0;
    bool armed_ = false;
};

}  // namespace mister::app
