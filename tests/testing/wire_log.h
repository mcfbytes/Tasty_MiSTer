// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "hal/spi_transport.h"

namespace mister::testing {

enum class WireKind : std::uint8_t { Sel, Desel, Word, Post, Fast, Blk };

struct WireEvent {
    WireKind kind = WireKind::Word;
    hal::ChipSelect cs = hal::ChipSelect::None;
    std::uint16_t out = 0;
    std::uint16_t reply = 0;
    bool reply_ok = false;
    std::size_t word = 0;
};

struct WireView {
    std::span<const std::uint16_t> words, io_heads;
    std::span<const std::size_t> words_at_head;
    std::span<const WireEvent> wire;
};

struct WireLog {
    std::vector<std::uint16_t> words;
    std::vector<hal::ChipSelect> word_cs;
    std::vector<std::size_t> select_at;
    std::vector<std::uint16_t> io_heads;
    std::vector<std::size_t> words_at_head;
    std::vector<WireEvent> wire;

    void select(hal::ChipSelect cs) {
        cur_cs_ = cs;
        io_head_ = cs == hal::ChipSelect::Io;
        select_at.push_back(words.size());
        wire.push_back(WireEvent{.kind = WireKind::Sel, .cs = cs, .word = words.size()});
    }

    void deselect() {
        wire.push_back(WireEvent{.kind = WireKind::Desel, .cs = cur_cs_, .word = words.size()});
        cur_cs_ = hal::ChipSelect::None;
        io_head_ = false;
    }

    void word(WireKind kind, hal::SpiWord out, std::optional<hal::SpiWord> reply) {
        words.push_back(out.v);
        word_cs.push_back(cur_cs_);
        if (io_head_) {
            words_at_head.push_back(words.size() - 1);
            io_heads.push_back(out.v);
            io_head_ = false;
        }
        WireEvent e{};
        e.kind = kind;
        e.cs = cur_cs_;
        e.out = out.v;
        e.word = words.size() - 1;
        if (reply.has_value()) {
            e.reply = reply->v;
            e.reply_ok = true;
        }
        wire.push_back(e);
    }

    void clear() {
        words.clear();
        word_cs.clear();
        select_at.clear();
        io_heads.clear();
        words_at_head.clear();
        wire.clear();
        cur_cs_ = hal::ChipSelect::None;
        io_head_ = false;
    }

    [[nodiscard]] WireView view() const { return WireView{words, io_heads, words_at_head, wire}; }

private:
    hal::ChipSelect cur_cs_ = hal::ChipSelect::None;
    bool io_head_ = false;
};

}
