// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/cas_feed.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

#include "cores/atari8/atari8_decl.h"
#include "cores/atari8/atari8_status2.h"
#include "cores/atari8/atari8_wire.h"
#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/byte_view.h"
#include "infra/message_sum.h"
#include "infra/span_cut.h"
#include "svc/read_bounded.h"

namespace mister::cores::atari8 {

namespace {

constexpr proto::FileSize kHeaderBytes{8};
constexpr infra::ByteCount kSignatureBytes{4};
constexpr infra::ByteOffset kLengthAt{4};
constexpr infra::ByteOffset kAuxAt{6};

constexpr proto::FileSize kByte{1};
constexpr proto::FileSize kPwmsLength{2};

constexpr proto::FileSize kFskGroup{2};
constexpr proto::FileSize kPwmcGroup{3};
constexpr proto::FileSize kPwmlGroup{2};

constexpr TapeRate kDefaultRate{600};
constexpr TapeRate kPerMs{1000};
constexpr TapeRate kFskUnitRate{10000};

constexpr proto::ReplyIndex kResetStatus2{2};
constexpr proto::ReplyIndex kResetStatus3{3};
constexpr proto::ReplyIndex kStatusAlone{};

[[nodiscard]] hal::LinkWord kept_reply(
    const proto::user_io::PollFrame& replies,
    proto::ReplyIndex i
) noexcept {
    return infra::affine::in_bounds(replies.kept, i) ? replies.replies[i] : hal::LinkWord{};
}

std::vector<std::byte> read_tape(svc::IFile* file) {
    if (file == nullptr)
        return {};
    const auto size = file->size();
    if (!size || *size > CasFeed::kMaxTapeBytes)
        return {};
    auto bytes = svc::read_bounded(*file, CasFeed::kMaxTapeBytes.file_word<std::uint64_t>());
    return bytes ? std::move(*bytes) : std::vector<std::byte>{};
}

}

static_assert(
    (proto::run_command_words(proto::MailboxRunWords::Pair) + kTapeNotFull.max_words())
            .times(CasFeed::kPulsesPerRun)
        <= proto::kMailboxDirectWords,
    "a whole run of pulses, each behind its guard read, fits the direct round's bound"
);
static_assert(
    Atari8Status3{hal::LinkWord{0b001}}.core_hz() == ClockHz{28644608}
        && !Atari8Status3{hal::LinkWord{0b111}}.core_hz().valid(),
    "atari800.cpp reboot_800: STATUS3's clock code, an unknown code 0"
);

CasFeed::Walk::Chunk CasFeed::Walk::chunk_of_(std::span<const std::byte> signature) noexcept {
    struct Name {
        std::string_view signature;
        Chunk kind;
    };

    static constexpr auto kNames = std::to_array<Name>({
        {"FUJI", Chunk::Fuji},
        {"baud", Chunk::Baud},
        {"data", Chunk::Data},
        {"fsk ", Chunk::Fsk},
        {"pwms", Chunk::Pwms},
        {"pwmc", Chunk::Pwmc},
        {"pwmd", Chunk::Pwmd},
        {"pwml", Chunk::Pwml},
    });
    const std::string_view text = infra::as_text(signature);
    for (const Name& n : kNames)
        if (n.signature == text)
            return n.kind;
    return Chunk::Other;
}

proto::FileOffset CasFeed::Walk::end_() const noexcept {
    return proto::kFileStart + proto::FileSize::of(std::span(file_));
}

bool CasFeed::Walk::holds_(proto::FileOffset at, proto::FileSize n) const noexcept {
    return at <= end_() && at.remaining_in(proto::FileSize::of(std::span(file_))) >= n;
}

std::optional<CasFeed::Walk::Header> CasFeed::Walk::header_at_(
    proto::FileOffset at
) const noexcept {
    if (!holds_(at, kHeaderBytes))
        return std::nullopt;
    const std::span<const std::byte> h = infra::affine::first(
        infra::affine::from(std::span<const std::byte>(file_), at),
        kHeaderBytes
    );
    const std::span<const std::byte> aux = infra::affine::from(h, kAuxAt);
    return Header{
        .kind = chunk_of_(infra::affine::first(h, kSignatureBytes)),
        .length = proto::FileSize{infra::load_le<std::uint16_t>(
            infra::affine::from(h, kLengthAt).first<sizeof(std::uint16_t)>()
        )},
        .aux = {aux[0], aux[1]}
    };
}

void CasFeed::Walk::emit_(TapeLevel level, TapeTicks ticks) {
    out_.push_back(Pulse{.entry = ticks.entry(level)});
}

std::vector<CasFeed::Pulse> CasFeed::Walk::take() noexcept {
    return std::exchange(out_, {});
}

std::byte CasFeed::Walk::byte_(proto::FileOffset at) const noexcept {
    if (!proto::FileSize::of(std::span(file_)).holds(at))
        return std::byte{};
    return infra::affine::from(std::span<const std::byte>(file_), at).front();
}

std::uint16_t CasFeed::Walk::word_(proto::FileOffset at) const noexcept {
    const std::array<std::byte, 2> b{byte_(at), byte_(at + kByte)};
    return infra::load_le<std::uint16_t>(std::span(b));
}

bool CasFeed::Walk::open(std::vector<std::byte> file, Atari8Status3 status3) {
    file_ = std::move(file);
    out_.clear();
    core_hz_ = status3.core_hz();
    offset_ = proto::kFileStart;
    mode_ = std::nullopt;
    prev_mode_ = std::nullopt;
    sample_ = TapeTicks::period_of(core_hz_, kDefaultRate);
    const std::optional<Header> first = header_at_(proto::kFileStart);
    if (!first || first->kind != Chunk::Fuji)
        return false;
    header_ = *first;
    const std::optional<proto::FileOffset> at = forward_(
        proto::kFileStart + kHeaderBytes + first->length
    );
    if (!at)
        return false;
    offset_ = *at;
    return true;
}

std::optional<proto::FileOffset> CasFeed::Walk::forward_(proto::FileOffset at) {
    bool ended = false;
    for (;;) {
        const std::optional<Header> next = header_at_(at);
        if (!next)
            return std::nullopt;
        header_ = *next;
        at += kHeaderBytes;
        walked_ = {};
        bool exit = false;
        switch (header_.kind) {
            case Chunk::Fuji:
                at += header_.length;
                break;
            case Chunk::Baud: {
                const TapeRate rate = header_.rate();
                if (!header_.length.empty() || rate == TapeRate{})
                    return std::nullopt;
                sample_ = TapeTicks::period_of(core_hz_, rate);
                break;
            }
            case Chunk::Data:
                prev_mode_ = mode_;
                mode_ = TapeMode::Standard;
                silence_ = header_.silence();
                group_ = kByte;
                exit = true;
                break;
            case Chunk::Fsk:
                prev_mode_ = mode_;
                mode_ = TapeMode::Standard;
                silence_ = header_.silence();
                group_ = kFskGroup;
                fsk_bit_ = TapeLevel::Low;
                exit = true;
                break;
            case Chunk::Pwms: {
                exit = true;
                if (header_.length != kPwmsLength) {
                    ended = true;
                    break;
                }
                const std::optional<PwmSettings> settings = header_.pwm();
                if (!settings) {
                    ended = true;
                    break;
                }
                pwm_bit_order_ = settings->order;
                pwm_bit_ = settings->mark;
                if (!holds_(at, kPwmsLength)) {
                    ended = true;
                    break;
                }
                const TapeRate rate{word_(at)};
                at += kPwmsLength;
                if (rate == TapeRate{}) {
                    ended = true;
                    break;
                }
                pwm_sample_ = TapeTicks::period_of(core_hz_, rate);
                exit = false;
                break;
            }
            case Chunk::Pwmc:
                prev_mode_ = mode_;
                mode_ = TapeMode::Turbo;
                silence_ = header_.silence();
                group_ = kPwmcGroup;
                exit = true;
                break;
            case Chunk::Pwmd:
                prev_mode_ = mode_;
                mode_ = TapeMode::Turbo;
                group_ = kByte;
                exit = true;
                break;
            case Chunk::Pwml:
                prev_mode_ = mode_;
                mode_ = TapeMode::Turbo;
                silence_ = header_.silence();
                group_ = kPwmlGroup;
                fsk_bit_ = pwm_bit_;
                exit = true;
                break;
            case Chunk::Other:
                break;
        }
        if (exit)
            break;
    }
    if (mode_ != prev_mode_)
        emit_(TapeLevel::High, TapeTicks{mode_ == TapeMode::Turbo ? 1u : 0u});
    if (ended)
        return std::nullopt;
    return at;
}

CasFeed::After CasFeed::Walk::block() {
    constexpr std::uint64_t kGroups = 256;
    constexpr std::uint64_t kTurboGroups = 128;
    const proto::FileSize to_read = (group_ * (mode_ == TapeMode::Turbo ? kTurboGroups : kGroups))
                                        .min(header_.length - walked_);
    if (!holds_(offset_, to_read))
        return After::End;
    const proto::FileOffset from = offset_;
    offset_ += to_read;
    const TapeLevel silence_bit = mode_ == TapeMode::Turbo ? pwm_bit_ : TapeLevel::High;
    while (silence_ != TapeSilence{}) {
        const TapeSilence len = std::min(silence_, kMaxSilence);
        emit_(silence_bit, TapeTicks::period_of(core_hz_, kPerMs) * len.count());
        silence_ -= len;
    }
    walked_ += to_read;
    for (proto::FileOffset at = from; at < offset_; at += group_) {
        switch (header_.kind) {
            case Chunk::Data: {
                emit_(TapeLevel::Low, sample_);
                for (const TapeLevel level : bits_of(byte_(at), BitOrder::LsbFirst))
                    emit_(level, sample_);
                emit_(TapeLevel::High, sample_);
                break;
            }
            case Chunk::Fsk:
            case Chunk::Pwml: {
                const std::uint32_t ld = word_(at);
                const TapeTicks unit = mode_ == TapeMode::Turbo
                    ? pwm_sample_
                    : TapeTicks::period_of(core_hz_, kFskUnitRate);
                if (ld)
                    emit_(fsk_bit_, unit * ld);
                fsk_bit_ = flipped(fsk_bit_);
                break;
            }
            case Chunk::Pwmc: {
                const std::uint16_t ld = word_(at + kByte);
                const TapeTicks half = (std::to_integer<std::uint32_t>(byte_(at)) * pwm_sample_)
                                           .halved();
                for (int j = 0; j < ld; ++j) {
                    emit_(pwm_bit_, half);
                    emit_(flipped(pwm_bit_), half);
                }
                break;
            }
            case Chunk::Pwmd: {
                for (const TapeLevel level : bits_of(byte_(at), pwm_bit_order_)) {
                    const TapeTicks half = (std::to_integer<std::uint32_t>(header_.pwm_width(level))
                                            * pwm_sample_)
                                               .halved();
                    emit_(pwm_bit_, half);
                    emit_(flipped(pwm_bit_), half);
                }
                break;
            }
            case Chunk::Fuji:
            case Chunk::Baud:
            case Chunk::Pwms:
            case Chunk::Other:
                break;
        }
    }
    if (offset_ == end_()) {
        emit_(mode_ == TapeMode::Turbo ? pwm_bit_ : TapeLevel::High, TapeTicks::kTail);
        return After::End;
    }
    if (walked_ == header_.length && offset_ < end_()) {
        const std::optional<proto::FileOffset> next = forward_(offset_);
        if (!next)
            return After::End;
        offset_ = *next;
        if (header_.kind == Chunk::Pwmc
            || header_.kind == Chunk::Data
            || silence_ != TapeSilence{}
            || mode_ != prev_mode_) {
            const TapeMode was = prev_mode_.value_or(*mode_);
            emit_(was == TapeMode::Turbo ? pwm_bit_ : TapeLevel::High, TapeTicks::kTail);
            return After::Gap;
        }
    }
    return After::More;
}

void CasFeed::begin_core() noexcept {
    TASTY_SEAT_BODY(CasFeed);
    walk_ = Walk{};
    file_.reset();
    step_ = Step::Idle;
    open_ = false;
    boot_ = false;
    pending_.clear();
}

void CasFeed::mount(std::unique_ptr<svc::IFile> file, bool boot) noexcept {
    TASTY_SEAT_BODY(CasFeed);
    file_ = std::move(file);
    boot_ = boot;
    open_ = false;
    pending_.clear();
    at_ = {};
    step_ = Step::Reset;
}

void CasFeed::unmount() noexcept {
    TASTY_SEAT_BODY(CasFeed);
    file_.reset();
    open_ = false;
    pending_.clear();
    step_ = Step::Idle;
}

proto::MailboxAct CasFeed::run_(ActBytes bytes, NotBefore at) noexcept {
    using wire::Level;
    using wire::ReadRegister;
    using wire::WriteRegister;
    switch (step_) {
        case Step::Idle:
            return {};
        case Step::Reset: {
            proto::MailboxRun run{bytes};
            (void)run.add(wire::set(WriteRegister::TapeReset, Level::High));
            (void)run.add(wire::set(WriteRegister::TapeReset, Level::Low));
            (void)run.add(wire::get(ReadRegister::Status2));
            (void)run.add(wire::get(ReadRegister::Status3));
            return infra::make<proto::MailboxAct>(
                proto::MailboxAct::CommandRun{.not_before = at, .commands = run.commands()}
            );
        }
        case Step::Pulses: {
            proto::MailboxRun run{bytes, proto::MailboxRunWords::Pair};
            auto pulse = pending_.begin();
            for (proto::ReplyIndex i{};
                 infra::affine::in_bounds(kPulsesPerRun, i) && pulse != pending_.end();
                 ++pulse, i += proto::ReplyCount{1})
                (void)run.add(wire::enqueue(pulse->entry));
            return infra::make<proto::MailboxAct>(proto::MailboxAct::CommandRun{
                .not_before = at,
                .commands = run.commands(),
                .guard = kTapeGuard,
                .words = proto::MailboxRunWords::Pair
            });
        }
        case Step::Keys: {
            proto::MailboxRun run{bytes};
            (void)run.add(wire::set(WriteRegister::OptionForce, Level::High));
            (void)run.add(wire::set(WriteRegister::StartForce, Level::High));
            (void)run.add(wire::set(WriteRegister::OptionForce, Level::Low));
            (void)run.add(wire::set(WriteRegister::StartForce, Level::Low));
            (void)run.add(wire::set(WriteRegister::SpaceForce, Level::High));
            (void)run.add(wire::set(WriteRegister::SpaceForce, Level::Low));
            return infra::make<proto::MailboxAct>(
                proto::MailboxAct::CommandRun{.not_before = at, .commands = run.commands()}
            );
        }
        case Step::Motor:
        case Step::Drain: {
            proto::MailboxRun run{bytes};
            (void)run.add(wire::get(ReadRegister::Status2));
            return infra::make<proto::MailboxAct>(
                proto::MailboxAct::CommandRun{.not_before = at, .commands = run.commands()}
            );
        }
        case Step::Pause:
            return infra::make<proto::MailboxAct>(proto::MailboxAct::CommandRun{.not_before = at});
    }
    return {};
}

proto::MailboxAct CasFeed::next(ActBytes bytes, NotBefore) noexcept {
    TASTY_SEAT_BODY(CasFeed);
    return run_(bytes, at_);
}

void CasFeed::settled(const proto::MailboxResult& result, ActBytes, NotBefore now) noexcept {
    TASTY_SEAT_BODY(CasFeed);
    const proto::MailboxResult::End end = result.end;
    const proto::user_io::PollFrame& replies = result.replies;
    if (step_ == Step::Pulses
        && (end == proto::MailboxResult::End::Done
            || end == proto::MailboxResult::End::GuardHeld)) {
        for (proto::ReplyIndex i{}; infra::affine::in_bounds(kPulsesPerRun, i)
             && infra::affine::in_bounds(replies.kept, i)
             && !pending_.empty();
             i += proto::ReplyCount{1})
            pending_.pop_front();
        at_ = end == proto::MailboxResult::End::GuardHeld ? now + kFullRetry : NotBefore{};
        if (pending_.empty())
            after_pulses_();
        return;
    }
    if (end != proto::MailboxResult::End::Done)
        return;
    at_ = {};
    switch (step_) {
        case Step::Idle:
        case Step::Pulses:
            return;
        case Step::Reset: {
            pause_ = Atari8Status2{kept_reply(replies, kResetStatus2)}.tape_slow() ? kSlowPause
                                                                                   : kPause;
            std::vector<std::byte> bytes = read_tape(file_.get());
            open_ = !bytes.empty()
                && walk_.open(std::move(bytes), Atari8Status3{kept_reply(replies, kResetStatus3)});
            const std::vector<Pulse> out = walk_.take();
            file_.reset();
            if (!open_) {
                step_ = Step::Idle;
                return;
            }
            pending_.assign(out.begin(), out.end());
            after_ = After::More;
            if (pending_.empty()) {
                after_pulses_();
            } else {
                step_ = Step::Pulses;
            }
            return;
        }
        case Step::Keys:
            boot_ = false;
            step_ = Step::Motor;
            return;
        case Step::Motor:
            if (!Atari8Status2{kept_reply(replies, kStatusAlone)}.tape_active()) {
                at_ = now + kMotorPoll;
                return;
            }
            walk_block_();
            return;
        case Step::Drain:
            if (!Atari8Status2{kept_reply(replies, kStatusAlone)}.tape_empty()) {
                at_ = now + kFullRetry;
                return;
            }
            step_ = Step::Pause;
            at_ = now + pause_;
            return;
        case Step::Pause:
            step_ = Step::Motor;
            return;
    }
}

void CasFeed::after_pulses_() noexcept {
    if (boot_) {
        step_ = Step::Keys;
        return;
    }
    switch (after_) {
        case After::More:
            step_ = Step::Motor;
            return;
        case After::Gap:
            step_ = Step::Drain;
            return;
        case After::End:
            break;
    }
    open_ = false;
    step_ = Step::Idle;
}

void CasFeed::walk_block_() noexcept {
    after_ = walk_.block();
    const std::vector<Pulse> out = walk_.take();
    pending_.assign(out.begin(), out.end());
    if (pending_.empty()) {
        after_pulses_();
        return;
    }
    step_ = Step::Pulses;
}

}
