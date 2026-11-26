// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "app/pcm/mailbox_slot.h"
#include "app/round_words.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/counter.h"
#include "os/clock.h"
#include "proto/link/link.h"
#include "proto/link/poll_frame_read.h"
#include "proto/link/spi_fio_queue.h"
#include "proto/link_sel.h"
#include "proto/disk_address.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_bus.h"
#include "proto/mailbox_programme.h"
#include "proto/mailbox_result.h"
#include "proto/mailbox_run.h"
#include "proto/uio_opcodes.h"

namespace mister::app::pcm {

namespace detail {

enum class ActEnd : std::uint8_t { Written, Reported, Held };

class ActWriter final {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Result = ActEnd;

    proto::link::Link& link;
    proto::link::SpiFioQueue& queue;
    const proto::MailboxProgramme& programme;
    const os::IClock& clock;
    MailboxSlot& slot;
    RoundWords& round_words;
    bool& direct_spent;
    bool& bus_spent;
    bool& wire_written;
    xthread::Counter& acts;
    xthread::Counter& refusals;
    xthread::Counter& none_acts;

    ActEnd on(const proto::MailboxAct::None&) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        none_acts.add();
        return ActEnd::Written;
    }

    ActEnd on(const proto::MailboxAct::Command& a) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        count_(queue.append_command(proto::command_words(a.opcode, a.words)).has_value());
        return ActEnd::Written;
    }

    ActEnd on(const proto::MailboxAct::Download& d) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        const std::span<const std::uint8_t, proto::kMailboxActBytes> bytes(slot.bytes);
        const proto::link::SpiFioQueue::Bracket b[] = {{d.index, d.len.of(bytes)}};
        count_(queue.append(b).has_value());
        return ActEnd::Written;
    }

    ActEnd on(const proto::MailboxAct::AddressedDownload& d) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        if (!due_(d.not_before))
            return ActEnd::Held;
        const std::span<const std::uint8_t, proto::kMailboxActBytes> bytes(slot.bytes);
        const proto::link::SpiFioQueue::Bracket b[] = {{d.index, d.len.of(bytes), d.address}};
        return report_(
            queue.append(b).has_value() ? proto::MailboxResult::End::Done
                                        : proto::MailboxResult::End::Refused
        );
    }

    ActEnd on(const proto::MailboxAct::CommandRun& r) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        if (!due_(r.not_before) || !direct_may_run_())
            return ActEnd::Held;
        const infra::OptRef<const proto::user_io::PollFrameShape> guard = r.guard.among(
            programme.guards()
        );
        if ((!guard && !r.guard.is_none()) || !run_fits_(r, guard))
            return report_(proto::MailboxResult::End::Refused);
        direct_spent = true;
        return report_(run_(r, guard));
    }

    ActEnd on(const proto::MailboxAct::AddressedUpload& u) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        if (!due_(u.not_before) || !direct_may_run_() || queue.held() || queue.tenant_holds_fio())
            return ActEnd::Held;
        const std::span<std::uint8_t> into = u.len.of(
            std::span<std::uint8_t, proto::kMailboxActBytes>(slot.bytes)
        );
        const proto::FioWordCount whole = queue.upload_words(proto::FileSize{into.size()});
        if (whole > proto::kMailboxDirectWords)
            return report_(proto::MailboxResult::End::Refused);
        direct_spent = true;
        const Ex<proto::FioWordCount> wrote = queue.upload(u.index, u.address, into);

        round_words.charge(wrote ? *wrote : whole);
        return report_(
            wrote ? proto::MailboxResult::End::Done : proto::MailboxResult::End::Faulted
        );
    }

    ActEnd on(const proto::MailboxAct::BusWrite& b) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        if (!due_(b.not_before) || bus_spent || !queue.idle())
            return ActEnd::Held;
        if (!b.len.fits())
            return report_(proto::MailboxResult::End::Refused);
        bus_spent = true;
        return report_(bus_write_(b));
    }

    ActEnd on(const proto::MailboxAct::BusRead& b) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        if (!due_(b.not_before) || bus_spent || !queue.idle())
            return ActEnd::Held;
        if (!b.len.fits())
            return report_(proto::MailboxResult::End::Refused);
        bus_spent = true;
        return report_(bus_read_(b));
    }

    ActEnd misrouted(const proto::MailboxAct&) noexcept {
        TASTY_SEAT_BODY(ActWriter);
        refusals.add();
        return ActEnd::Written;
    }

private:
    void count_(bool written) noexcept {
        wire_written = wire_written || written;
        if (written) {
            acts.add();
        } else {
            refusals.add();
        }
    }

    [[nodiscard]] ActEnd report_(proto::MailboxResult::End end) noexcept {
        slot.result.end = end;
        wire_written = wire_written || end != proto::MailboxResult::End::Refused;
        count_(
            end == proto::MailboxResult::End::Done || end == proto::MailboxResult::End::GuardHeld
        );
        return ActEnd::Reported;
    }

    [[nodiscard]] bool bus_open_(proto::UioOp opcode, proto::DiskAddress address) noexcept {
        round_words.charge(proto::user_io::kCommandBeatWords + proto::DiskAddress::kWords);
        const hal::LinkHalves at = address.halves();
        return link.command(proto::LinkSel::Io, proto::word(opcode))
            && link.word(at.low)
            && link.word(at.high);
    }

    [[nodiscard]] proto::MailboxResult::End bus_write_(
        const proto::MailboxAct::BusWrite& b
    ) noexcept {
        const std::span<const std::byte> payload = b.len.of(
            std::span<const std::byte, proto::kMailboxBulkBytes>(slot.bulk)
        );
        round_words.charge(b.len.link_words());
        if (!bus_open_(b.opcode, b.address))
            return bus_fault_();
        const bool words = b.len.framing == hal::Width::Word;
        const std::size_t step = words ? sizeof(hal::LinkWord) : 1;
        for (std::size_t at = 0; at + step <= payload.size(); at += step) {
            const hal::LinkWord w = words ? hal::LinkWord::load_le(payload.subspan(at).first<2>())
                                          : hal::LinkWord::of_byte(payload[at]);
            if (!link.word(w))
                return bus_fault_();
        }
        link.close();
        return proto::MailboxResult::End::Done;
    }

    [[nodiscard]] proto::MailboxResult::End bus_read_(
        const proto::MailboxAct::BusRead& b
    ) noexcept {
        const std::span<std::byte> into = b.len.of(
            std::span<std::byte, proto::kMailboxBulkBytes>(slot.bulk)
        );
        round_words.charge(b.len.link_words());
        if (!bus_open_(b.opcode, b.address))
            return bus_fault_();
        const bool words = b.len.framing == hal::Width::Word;
        const std::size_t step = words ? sizeof(hal::LinkWord) : 1;
        for (std::size_t at = 0; at + step <= into.size(); at += step) {
            const Ex<hal::LinkWord> reply = link.read();
            if (!reply)
                return bus_fault_();
            if (words) {
                reply->store_le(into.subspan(at).first<2>());
            } else {
                into[at] = reply->low_lane();
            }
        }
        link.close();
        return proto::MailboxResult::End::Done;
    }

    [[nodiscard]] proto::MailboxResult::End bus_fault_() noexcept {
        link.close();
        return proto::MailboxResult::End::Faulted;
    }

    [[nodiscard]] bool due_(proto::MailboxAct::NotBefore at) const noexcept {
        return at == proto::MailboxAct::NotBefore{} || clock.now() >= at;
    }

    [[nodiscard]] bool direct_may_run_() const noexcept { return !direct_spent && queue.idle(); }

    [[nodiscard]] bool run_fits_(
        const proto::MailboxAct::CommandRun& r,
        infra::OptRef<const proto::user_io::PollFrameShape> guard
    ) const noexcept {
        if (r.commands > proto::user_io::kMaxPollFrameReplies)
            return false;
        const proto::FioWordCount each = proto::run_command_words(r.words)
            + (guard ? guard->max_words() : proto::FioWordCount{});
        return each.times(r.commands) <= proto::kMailboxDirectWords;
    }

    [[nodiscard]] proto::MailboxResult::End run_(
        const proto::MailboxAct::CommandRun& r,
        infra::OptRef<const proto::user_io::PollFrameShape> guard
    ) noexcept {
        slot.result.replies = {};
        proto::MailboxRunReader commands{
            std::span<const std::uint8_t, proto::kMailboxActBytes>(slot.bytes),
            r.words
        };
        for (proto::ReplyCount done{}; done < r.commands; done += proto::ReplyCount{1}) {
            if (guard) {
                const proto::link::PollFrameRead asked = link.read_poll_frame(*guard, std::nullopt);
                round_words.charge(asked.charged);
                if (asked.outcome == proto::link::PollFrameRead::Outcome::Faulted)
                    return proto::MailboxResult::End::Faulted;
                if (asked.outcome == proto::link::PollFrameRead::Outcome::Held)
                    return proto::MailboxResult::End::GuardHeld;
            }
            const proto::MailboxRunCommand command = commands.next();
            round_words.charge(proto::run_command_words(r.words));
            if (!link.command(proto::LinkSel::Io, command.opcode))
                return proto::MailboxResult::End::Faulted;
            const Ex<hal::LinkWord> reply = link.exchange(command.word);
            if (!reply)
                return proto::MailboxResult::End::Faulted;
            if (r.words == proto::MailboxRunWords::Pair && !link.exchange(command.tail))
                return proto::MailboxResult::End::Faulted;
            link.close();
            slot.result.replies.keep(*reply);
        }
        return proto::MailboxResult::End::Done;
    }
};

}

}
