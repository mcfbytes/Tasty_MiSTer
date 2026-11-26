// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/minimig_ladder_frames.h"

#include <algorithm>
#include <array>
#include <utility>

#include "infra/endian.h"

namespace mister::cores::minimig {

namespace {

constexpr hal::LinkWord kZero{};
constexpr hal::LinkWord kOne{1};
constexpr std::array kDiscardFrame{
    word(FrameOp::Write),
    kZero,
    kZero,
    kZero,
    kZero,
    kZero,
    kZero,
    kZero,
    kZero,
    kOne,
    kOne,
    kOne,
    kOne
};

constexpr hal::LinkWord byte_word(std::byte b) noexcept {
    return hal::LinkWord{std::to_integer<std::uint8_t>(b)};
}

}

void FrameQueue::push_(std::span<const hal::LinkWord> frame) {
    const std::size_t bytes = frame.size_bytes();

    const WireChunk frame_chunk = WireChunk::of(bytes);
    if (runs_.empty() || runs_.back().frame != frame_chunk) {
        runs_.push_back(Run{.bytes = {}, .frame = frame_chunk});
    }
    std::vector<std::uint8_t>& out = runs_.back().bytes;
    const std::size_t at = out.size();
    out.resize(at + bytes);
    std::span<std::uint8_t> dst = std::span(out).subspan(at);
    for (const hal::LinkWord w : frame) {
        w.store_le(dst.first<sizeof(hal::LinkWord)>());
        dst = dst.subspan(sizeof(hal::LinkWord));
    }
}

void FrameQueue::reg(FrameOp op, hal::LinkWord parm) {
    const std::array<hal::LinkWord, 2> frame{word(op), parm};
    push_(frame);
}

void FrameQueue::mem(BusAddr at, std::span<const std::byte> data) {
    scratch_.clear();
    scratch_.push_back(word(FrameOp::Write));
    for (const std::byte b : at.wire_bytes())
        scratch_.push_back(byte_word(b));
    for (const std::byte b : data)
        scratch_.push_back(byte_word(b));
    push_(scratch_);
}

void FrameQueue::mem_blocks(BusAddr at, std::span<const std::byte> data) {
    while (!data.empty()) {
        const std::span<const std::byte> head = data.first(std::min(data.size(), kBlockBytes));
        std::array<std::byte, kBlockBytes> block{};
        std::ranges::copy(head, block.begin());
        mem(at, block);
        at += BusBytes::of(block);
        data = data.subspan(head.size());
    }
}

void FrameQueue::mem_zeros(BusAddr at, BusBytes len) {
    constexpr std::array<std::byte, kBlockBytes> kBlank{};
    for (const BusAddr end = at + len; at < end; at += BusBytes::of(kBlank))
        mem(at, kBlank);
}

void FrameQueue::mem_words(BusAddr at, std::span<const std::uint16_t> words) {
    std::vector<std::byte> bytes(words.size_bytes());
    std::span<std::byte> dst{bytes};
    for (const std::uint16_t w : words) {
        infra::store_be<std::uint16_t>(dst.first<sizeof(std::uint16_t)>(), w);
        dst = dst.subspan(sizeof(std::uint16_t));
    }
    mem(at, bytes);
}

void FrameQueue::discard_residents() {
    push_(kDiscardFrame);
}

void FrameQueue::ext_cfg(hal::LinkHalves halves) {
    const std::array<hal::LinkWord, 5>
        frame{word(FrameOp::Status), hal::LinkWord{}, hal::LinkWord{}, halves.low, halves.high};
    push_(frame);
}

FrameQueue::Run FrameQueue::take() {
    Run run = std::move(runs_.front());
    runs_.pop_front();
    return run;
}

}
