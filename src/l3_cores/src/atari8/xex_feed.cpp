// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/xex_feed.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "cores/atari8/atari8_wire.h"
#include "cores/atari8/sio_dma.h"
#include "cores/atari8/xex_loaders.h"
#include "infra/endian.h"
#include "infra/span_cut.h"
#include "proto/user_io/poll_frame.h"

namespace mister::cores::atari8 {

namespace {

using wire::Level;
using wire::WriteRegister;

constexpr HostAddress kLoaderAt{0xD100};
constexpr HostAddress kInitVector{0x2E2};
constexpr HostAddress kRunVector{0x2E0};
constexpr std::byte kLoaderReady{0x60};
constexpr std::byte kLoaderDone{0x5F};
constexpr std::byte kSegmentReady{0x01};
constexpr std::byte kFileEnded{0xFF};
constexpr HostAddress kNoAddress{0xFFFF};

constexpr std::array<std::byte, 2> kLoaderReturn = {std::byte{0x00}, std::byte{0xD1}};

constexpr Atari8Fill kArmClear = Atari8Fill::kComputerRam.cleared();

constexpr Atari8Address kOsMark{0xC006};
constexpr std::array<std::byte, 4> kBriansOs =
    {std::byte{0x42}, std::byte{0x42}, std::byte{0x00}, std::byte{0x64}};

constexpr HostAddress kLoaderStatus = kLoaderAt.at(XexFeed::kStatusAt);

constexpr std::array<std::byte, 1> kClearVector{std::byte{0x00}};
constexpr std::array<std::byte, 1> kBasicfVector{std::byte{0x01}};
constexpr std::array<std::byte, 1> kBootFlagVector{std::byte{0x02}};
constexpr std::array<std::byte, 2> kCasiniVector{std::byte{0x01}, std::byte{0xD1}};
constexpr std::array<std::byte, 2> kDosvecVector{std::byte{0x71}, std::byte{0xE4}};
constexpr std::array<HostPoke, 6> kVectors = {{
    {.at = host_at(HostAddress{0x244}), .bytes = kClearVector},
    {.at = host_at(HostAddress{0x3FA}), .bytes = kClearVector},
    {.at = host_at(HostAddress{0x3F8}), .bytes = kBasicfVector},
    {.at = host_at(HostAddress{0x009}), .bytes = kBootFlagVector},
    {.at = host_at(HostAddress{0x002}), .bytes = kCasiniVector},
    {.at = host_at(HostAddress{0x00A}), .bytes = kDosvecVector},
}};

constexpr std::array<std::byte, 1> kHandlerByte{std::byte{0x5C}};
constexpr std::array<std::byte, 2> kBriansPupbt{std::byte{0x93}, std::byte{0x25}};
constexpr std::array<std::byte, 3> kOsPupbt{std::byte{0x5C}, std::byte{0x93}, std::byte{0x25}};
constexpr std::array<std::byte, 1> kHookByte{std::byte{0x60}};
constexpr std::array<HostPoke, 3> kBriansPatches = {{
    {.at = Atari8Address{0x245}, .bytes = kHandlerByte},
    {.at = host_at(HostAddress{0x33E}), .bytes = kBriansPupbt},
    {.at = Atari8Address{0x3ED}, .bytes = kHookByte},
}};
constexpr std::array<HostPoke, 2> kOsPatches = {{
    {.at = host_at(HostAddress{0x33D}), .bytes = kOsPupbt},
    {.at = Atari8Address{0x3ED}, .bytes = kHookByte},
}};

}

void XexFeed::close() noexcept {
    TASTY_SEAT_BODY(XexFeed);
    file_.reset();
    step_ = Step::Closed;
    released_ = false;
}

void XexFeed::open(std::unique_ptr<svc::IFile> file) noexcept {
    TASTY_SEAT_BODY(XexFeed);
    file_ = std::move(file);
    step_ = file_ ? Step::Booting : Step::Closed;
    at_ = proto::kFileStart;
    first_ = true;
    released_ = false;
}

bool XexFeed::wants_boot() const noexcept {
    TASTY_SEAT_BODY(XexFeed);
    return step_ == Step::Booting;
}

void XexFeed::boot_ordered() noexcept {
    TASTY_SEAT_BODY(XexFeed);
    if (step_ == Step::Booting)
        step_ = Step::Rebooting;
}

void XexFeed::booted() noexcept {
    TASTY_SEAT_BODY(XexFeed);
    if (step_ == Step::Rebooting)
        step_ = Step::Arm;
}

bool XexFeed::rebooting() const noexcept {
    TASTY_SEAT_BODY(XexFeed);
    return step_ == Step::Rebooting;
}

bool XexFeed::take_release() noexcept {
    TASTY_SEAT_BODY(XexFeed);
    return std::exchange(released_, false);
}

bool XexFeed::wants() const noexcept {
    TASTY_SEAT_BODY(XexFeed);
    return step_ != Step::Closed && step_ != Step::Booting && step_ != Step::Rebooting;
}

bool XexFeed::arming() const noexcept {
    TASTY_SEAT_BODY(XexFeed);
    switch (step_) {
        case Step::Arm:
        case Step::Clear:
        case Step::Loader:
        case Step::Vectors:
        case Step::Probe:
        case Step::Patch:
        case Step::Release:
            return true;
        case Step::Closed:
        case Step::Booting:
        case Step::Rebooting:
        case Step::Poll:
        case Step::InitVector:
        case Step::RunVector:
        case Step::Segment:
        case Step::Next:
        case Step::End:
            return false;
    }
    return false;
}

XexFeed::Step XexFeed::step() const noexcept {
    TASTY_SEAT_BODY(XexFeed);
    return step_;
}

proto::MailboxAct XexFeed::next(ActBytes bytes) {
    TASTY_SEAT_BODY(XexFeed);
    proto::MailboxRun run{bytes};
    switch (step_) {
        case Step::Closed:
        case Step::Booting:
        case Step::Rebooting:
            return {};
        case Step::Arm:
            (void)run.add(wire::set(WriteRegister::XexLoader, Level::High));
            (void)run.add(wire::get(wire::ReadRegister::Status1));
            return wire::run_act(run);
        case Step::Clear:
            return infra::make<proto::MailboxAct>(clear_.block(bytes));
        case Step::Loader:
            return dma_write(bytes, host_at(kLoaderAt), kXexDmaLoader);
        case Step::Vectors:
            return dma_write(bytes, pokes_.front().at, pokes_.front().bytes);
        case Step::Probe:
            return dma_read(kOsMark, infra::ByteCount{kBriansOs.size()});
        case Step::Patch:
            return dma_write(bytes, pokes_.front().at, pokes_.front().bytes);
        case Step::Release:
            (void)run.add(wire::pause(wire::Pause::Run));
            return wire::run_act(run);
        case Step::Poll:
            return dma_read(host_at(kLoaderAt), kPollBytes);
        case Step::InitVector:
            return dma_write(bytes, host_at(kInitVector), kLoaderReturn);
        case Step::RunVector:
            return dma_write(bytes, host_at(kRunVector), start_.wire_bytes());
        case Step::Segment: {
            const infra::ByteCount n = std::min(left_, infra::ByteCount{bytes.size()});
            const std::span<std::byte> all = std::as_writable_bytes(bytes);
            const std::span<std::byte> into = n.first_in(all);
            if (!file_ || !file_->read_exact_at(at_, into)) {
                step_ = Step::End;
                return next(bytes);
            }
            return dma_written(host_at(load_), n);
        }
        case Step::Next:
            return dma_write(bytes, host_at(kLoaderStatus), std::span{&kSegmentReady, 1});
        case Step::End:
            return dma_write(bytes, host_at(kLoaderStatus), std::span{&kFileEnded, 1});
    }
    return {};
}

void XexFeed::settled(const proto::MailboxResult& result, ActBytes bytes) {
    TASTY_SEAT_BODY(XexFeed);
    if (result.end != proto::MailboxResult::End::Done)
        return;
    switch (step_) {
        case Step::Closed:
        case Step::Booting:
        case Step::Rebooting:
            return;
        case Step::Arm: {
            const proto::user_io::PollFrame& r = result.replies;
            if (r.kept != proto::ReplyCount{})
                status_ = Atari8Status{r.replies.prefix(r.kept).back()};
            clear_ = kArmClear;
            step_ = Step::Clear;
            return;
        }
        case Step::Clear:
            clear_ = clear_.after_block();
            if (clear_.done())
                step_ = Step::Loader;
            return;
        case Step::Loader:
            pokes_ = kVectors;
            step_ = Step::Vectors;
            return;
        case Step::Vectors:
            pokes_ = pokes_.subspan(1);
            if (pokes_.empty())
                step_ = Step::Probe;
            return;
        case Step::Probe: {
            std::array<std::byte, kBriansOs.size()> mark{};
            std::ranges::copy(
                std::as_bytes(std::span{bytes}.first(kBriansOs.size())),
                mark.begin()
            );
            patches_(mark == kBriansOs);
            step_ = pokes_.empty() ? Step::Release : Step::Patch;
            return;
        }
        case Step::Patch:
            pokes_ = pokes_.subspan(1);
            if (pokes_.empty())
                step_ = Step::Release;
            return;
        case Step::Release:
            released_ = true;
            step_ = Step::Poll;
            return;
        case Step::Poll:
            if (std::byte{bytes[0]} == kLoaderReady
                && !infra::affine::from(bytes, kStatusAt).front()) {
                step_ = Step::InitVector;
            } else if (std::byte{bytes[0]} == kLoaderDone) {
                step_ = Step::End;
            }
            return;
        case Step::InitVector:
            read_start_();
            return;
        case Step::RunVector:
            first_ = false;
            read_end_();
            return;
        case Step::Segment: {
            const infra::ByteCount n = std::min(left_, infra::ByteCount{bytes.size()});
            at_ += proto::FileSize::of(n);
            load_ = load_.after(n);
            left_ -= n;
            if (left_ == infra::ByteCount{})
                step_ = Step::Next;
            return;
        }
        case Step::Next:
            step_ = Step::Poll;
            return;
        case Step::End:
            close();
            return;
    }
}

std::optional<HostAddress> XexFeed::read_pair_() noexcept {
    std::array<std::byte, 2> pair{};
    if (!file_ || !file_->read_exact_at(at_, pair))
        return std::nullopt;
    at_ += proto::FileSize::of(infra::ByteCount{pair.size()});
    return HostAddress::load_le(pair);
}

void XexFeed::read_start_() noexcept {
    std::optional<HostAddress> start = kNoAddress;
    while (start == kNoAddress) {
        start = read_pair_();
        if (!start) {
            end_();
            return;
        }
    }
    start_ = *start;
    load_ = *start;
    if (first_) {
        step_ = Step::RunVector;
        return;
    }
    read_end_();
}

void XexFeed::read_end_() noexcept {
    const std::optional<HostAddress> last = read_pair_();
    const std::optional<infra::ByteCount> length = last ? load_.through(*last) : std::nullopt;
    if (!length) {
        end_();
        return;
    }
    left_ = *length;
    step_ = Step::Segment;
}

void XexFeed::end_() noexcept {
    step_ = Step::End;
}

void XexFeed::patches_(bool brians) noexcept {
    if (brians) {
        pokes_ = kBriansPatches;
    } else if (status_.mode_800()) {
        pokes_ = {};
    } else {
        pokes_ = kOsPatches;
    }
}

}
