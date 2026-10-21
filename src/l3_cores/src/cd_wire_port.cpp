// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_wire_port.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "cd_core_detail.h"
#include "cores/cd_engine.h"
#include "cores/cdwire.h"
#include "hal/boards_table.h"
#include "hal/link_timing.h"
#include "hal/selected.h"
#include "infra/byte_view.h"
#include "infra/error.h"
#include "proto/image_bracket.h"
#include "proto/spi_fio_queue.h"
#include "reactor/tick.h"

#ifndef TASTY_CDDA_DUMP_ENABLED
#define TASTY_CDDA_DUMP_ENABLED 0
#endif

namespace mister::cores {

namespace {

constexpr std::uint32_t kSubcodeBracketWords = 2 + 2 + (1 + (kSubBytes + 2) / 2) + 2;
constexpr std::uint32_t kFirstRoundWords = kSubcodeBracketWords
    + 2
    + 2
    + 1
    + proto::SpiFioQueue::kPieceWords;
static_assert(kSubcodeBracketWords == 56 && kFirstRoundWords == 453);
static_assert(
    (kSubBytes + 2) + 2 * kRawFrame <= proto::SpiFioQueue::kSlabBytes,
    "the primed pair and its subcode frame fit one act"
);

constexpr std::size_t kInlineFrameBytes = 128;
constexpr std::uint32_t kInlineFrameWords = 2 + 2 + 1 + kInlineFrameBytes + 2;

consteval bool cd_split_fits_every_board() {
    return hal::every_measured(&hal::BoardProfile::timing, [](const hal::LinkTiming& g) consteval {
        const auto t = hal::LinkTimingValues::measured(g);
        return kFirstRoundWords <= t.round_budget_words
            && hal::round_cost_ns(
                   t.word_ns,
                   t.base_round_ns,
                   t.sleep_ns,
                   t.round_budget_words + kInlineFrameWords
               )
            < static_cast<std::uint64_t>(reactor::kTickNs);
    });
}

static_assert(
    cd_split_fits_every_board(),
    "the tick round drains the subcode bracket and the first piece together, and an "
    "inline frame beside a full drain stays under 1 ms"
);

constexpr std::size_t kStatusCommandWords = 1 + CdEngine::kMaxStatusWords;

}

struct CdWirePort::CddaCapture {
    std::vector<std::uint8_t> buf;
    std::size_t used = 0;
    std::string path;
    bool spent = false;

    void append(std::span<const std::byte> p) {
        if (spent)
            return;
        const std::size_t need = p.size() + 4u;
        if (used + need > buf.size()) {
            spent = true;
            return;
        }
        const auto n = static_cast<std::uint32_t>(p.size());
        buf[used + 0] = static_cast<std::uint8_t>(n & 0xFFu);
        buf[used + 1] = static_cast<std::uint8_t>((n >> 8) & 0xFFu);
        buf[used + 2] = static_cast<std::uint8_t>((n >> 16) & 0xFFu);
        buf[used + 3] = static_cast<std::uint8_t>((n >> 24) & 0xFFu);
        std::memcpy(buf.data() + used + 4, p.data(), p.size());
        used += need;
    }

    void flush() {
#if TASTY_CDDA_DUMP_ENABLED
        if (used == 0 || path.empty())
            return;
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (f == nullptr)
            return;
        std::fwrite(buf.data(), 1, used, f);
        std::fclose(f);
        std::fprintf(stderr, "[cdda-dump] wrote %zu bytes to %s\n", used, path.c_str());
#endif
    }
};

CdWirePort::CdWirePort(const CdProfile& cd, const HostServices& host)
    : cd_(cd),
      link_(host.link),
      fio_queue_(host.fio_queue) {
#if TASTY_CDDA_DUMP_ENABLED
    if (const char* dump = std::getenv("TASTY_CDDA_DUMP"); dump != nullptr && *dump != '\0') {
        const char* mb = std::getenv("TASTY_CDDA_DUMP_MB");
        std::size_t megs = (mb != nullptr) ? std::strtoul(mb, nullptr, 10) : 16u;
        if (megs == 0 || megs > 128)
            megs = 16;
        cdda_cap_ = std::make_unique<CddaCapture>();
        cdda_cap_->path = dump;
        cdda_cap_->buf.resize(megs * 1024u * 1024u);
        std::fprintf(stderr, "[cdda-dump] armed: %zu MB -> %s\n", megs, dump);
    }
#endif
}

CdWirePort::~CdWirePort() {
    if (cdda_cap_)
        cdda_cap_->flush();
}

Ex<void> CdWirePort::send_data(
    IoIndex io_index,
    std::span<const std::byte> payload,
    proto::IImageSink& sink
) {
    TASTY_SEAT_BODY(CdWirePort);
    const auto q = fio_queue_;
    if (q && (!q->idle() || payload.size() > kInlineFrameBytes)) {
        const proto::SpiFioQueue::Bracket b{
            proto::WideIoIndex{io_index.v},
            infra::byte_cast<const std::uint8_t>(payload)
        };
        if (q->append(std::span(&b, 1))) {
            if (cdda_cap_ && io_index.v == cd_.io.cdda.v)
                cdda_cap_->append(payload);
            return {};
        }
        (void)q->flush();
    }
    return bracket_inline(io_index, payload, sink);
}

Ex<void> CdWirePort::bracket_inline(
    IoIndex io_index,
    std::span<const std::byte> payload,
    proto::IImageSink& sink
) {
    if (cdda_cap_ && io_index.v == cd_.io.cdda.v) {
        cdda_cap_->append(payload);
    }
    auto ds = proto::ImageBracket::open(sink, proto::WideIoIndex{io_index.v});
    if (!ds)
        return std::unexpected(ds.error());

    if (auto w = ds->write(infra::byte_cast<const std::uint8_t>(payload)); !w) {
        return w;
    }
    return ds->end();
}

bool CdWirePort::egress_idle() const noexcept {
    TASTY_SEAT_BODY(CdWirePort);
    return !fio_queue_ || fio_queue_->idle();
}

std::uint32_t CdWirePort::egress_abandons() const noexcept {
    return fio_queue_ ? fio_queue_->stats().abandons : 0u;
}

Ex<void> CdWirePort::submit_sector(const ICdSectorEgress::Act& act) {
    TASTY_SEAT_BODY(CdWirePort);
    const auto q = fio_queue_;
    if (!q)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), act.index.v});
    if (cdda_cap_ && act.index.v == cd_.io.cdda.v)
        cdda_cap_->append(std::as_bytes(act.sector));
    std::array<proto::SpiFioQueue::Bracket, 2> b{};
    std::size_t n = 0;
    if (!act.subcode.empty())
        b[n++] = {proto::WideIoIndex{act.sub_index.v}, act.subcode};
    b[n++] = {proto::WideIoIndex{act.index.v}, act.sector};
    return q->submit(std::span(b.data(), n));
}

bool CdWirePort::can_send_data(std::uint16_t selector) {
    TASTY_SEAT_BODY(CdWirePort);
    hal::ISpiTransport& link = link_;
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(hal::SpiWord{cdwire::kUioCdGet}); !r) {
        ++wire_faults_;
        return false;
    }
    if (auto r = link.transfer(hal::SpiWord{selector}); !r) {
        ++wire_faults_;
        return false;
    }
    const auto answer = link.transfer(hal::SpiWord{0});
    if (!answer) {
        ++wire_faults_;
        return false;
    }
    return answer->v == 1;
}

Ex<void> CdWirePort::send_status(std::span<const std::uint16_t> words) {
    TASTY_SEAT_BODY(CdWirePort);
    const std::size_t n = words.size();

    if (const auto q = fio_queue_; q && !q->idle()) {
        std::uint16_t cmd[kStatusCommandWords] = {cdwire::kUioCdSet};
        for (std::size_t i = 0; i < n; ++i)
            cmd[1 + i] = words[i];
        if (q->append_command(std::span<const std::uint16_t>(cmd, 1 + n)))
            return {};
        (void)q->flush();
    }
    hal::ISpiTransport& link = link_;
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(hal::SpiWord{cdwire::kUioCdSet}); !r) {
        return std::unexpected(r.error());
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (auto r = link.transfer(hal::SpiWord{words[i]}); !r) {
            return std::unexpected(r.error());
        }
    }
    return {};
}

void CdWirePort::service_command_edge(CdEngine& eng) noexcept {
    TASTY_SEAT_BODY(CdWirePort);
    const std::size_t nwords = eng.command_word_count();

    std::uint16_t words[CdEngine::kMaxCommandWords] = {};
    bool wanted = false;
    bool failed = false;
    {
        hal::ISpiTransport& link = link_;
        hal::Selected cs(link, hal::ChipSelect::Io);
        auto first = link.transfer(hal::SpiWord{cdwire::kUioCdGet});
        if (!first) {
            ++wire_faults_;
            failed = true;
        } else {
            const auto req = static_cast<std::uint8_t>(first->v & 0xFFu);

            static_cast<void>(eng.on_command_edge(req));
            wanted = eng.command_wanted();
            if (const auto sel = eng.command_selector(); wanted && sel) {

                if (auto r = link.transfer(hal::SpiWord{*sel}); !r) {
                    ++wire_faults_;
                    failed = true;
                }
            }
            if (wanted && !failed) {
                for (std::size_t i = 0; i < nwords; ++i) {
                    auto r = link.transfer(hal::SpiWord{0});
                    if (!r) {
                        ++wire_faults_;
                        failed = true;
                        break;
                    }
                    words[i] = r->v;
                }
            }
        }
    }

    if (wanted && !failed) {
        if (auto r = eng.accept_command(std::span<const std::uint16_t>(words, nwords)); !r) {
            ++wire_faults_;
        }
    }

    eng.after_command_round();
}

void CdWirePort::declare_resets(const proto::ResetTerms& terms) noexcept {
    if (fio_queue_)
        fio_queue_->declare_resets(terms);
}

}
