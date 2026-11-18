// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

#include "hal/core_capabilities.h"
#include "hal/core_identity.h"
#include "hal/core_signals.h"
#include "hal/spi_transport.h"
#include "infra/error.h"
#include "wire_log.h"

namespace mister::testing {

class RecordingTransport : public hal::ISpiTransport, public hal::ICoreSignals {
public:
    WireLog log;
    std::deque<std::uint16_t> replies;
    hal::SpiWord idle_reply{0};
    hal::CoreIdentity identity{hal::CoreTypeByte{0xA4}};
    hal::CoreCapabilities caps{};
    hal::Width fio_width = hal::Width::Byte;
    hal::IoVersion io_ver{};
    bool record_blocks = false;
    std::vector<std::vector<std::uint8_t>> blocks;
    std::size_t identify_calls = 0;
    std::size_t gpo_clears = 0;
    std::size_t reset_asserts = 0;
    std::size_t reset_releases = 0;

    void fail_after(std::size_t transfers, Errc code = Errc::spi_timeout) {
        fail_at_ = transfers;
        fail_code_ = code;
        fail_armed_ = true;
        transfers_ = 0;
    }

    void select(hal::ChipSelect cs) override { log.select(cs); }

    void deselect() override { log.deselect(); }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

    [[nodiscard]] Ex<hal::SpiWord> transfer(hal::SpiWord out) override {
        ++transfers_;
        if (fail_armed_ && transfers_ == fail_at_ + 1) {
            fail_armed_ = false;
            log.word(WireKind::Word, out, std::nullopt);
            return std::unexpected(Error{fail_code_, ERR_SITE(), out.v});
        }
        const hal::SpiWord reply = answer(out);
        log.word(WireKind::Word, out, reply);
        return reply;
    }

#pragma GCC diagnostic pop

    [[nodiscard]] hal::Width width() const noexcept override { return fio_width; }

    [[nodiscard]] hal::IoVersion io_version() const noexcept override { return io_ver; }

    [[nodiscard]] Ex<void> block_write(std::span<const std::uint8_t> data) override {
        blocks.emplace_back(data.begin(), data.end());
        if (!record_blocks)
            return {};
        const WireKind kind = fio_width == hal::Width::Word ? WireKind::Fast : WireKind::Blk;
        for (std::size_t i = 0; i < data.size(); i += 2) {
            const auto lo = static_cast<std::uint16_t>(data[i]);
            const auto hi = static_cast<std::uint16_t>(i + 1 < data.size() ? data[i + 1] : 0);
            const auto word = static_cast<std::uint16_t>(lo | (hi << 8));
            log.word(kind, hal::SpiWord{word}, std::nullopt);
        }
        return {};
    }

    [[nodiscard]] Ex<void> block_read(std::span<std::uint8_t>) override { return {}; }

    [[nodiscard]] Ex<void> post(hal::SpiWord out) override {
        log.word(WireKind::Post, out, answer(out));
        return {};
    }

    [[nodiscard]] Ex<hal::CoreIdentity> identify() override {
        ++identify_calls;
        return identity;
    }

    hal::CoreCapabilities latch_capabilities() override { return caps; }

    void set_core_reset(bool asserted) override {
        if (asserted) {
            ++reset_asserts;
        } else {
            ++reset_releases;
        }
    }

    void clear_gpo() override { ++gpo_clears; }

protected:
    virtual hal::SpiWord answer(hal::SpiWord out) {
        (void)out;
        if (replies.empty())
            return idle_reply;
        const std::uint16_t reply = replies.front();
        replies.pop_front();
        return hal::SpiWord{reply};
    }

private:
    std::size_t transfers_ = 0;
    std::size_t fail_at_ = 0;
    bool fail_armed_ = false;
    Errc fail_code_ = Errc::spi_timeout;
};

}
