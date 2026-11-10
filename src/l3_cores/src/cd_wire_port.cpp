// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_wire_port.h"

#include <array>
#include <chrono>
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

constexpr std::uint32_t kSubcodeBracketWords = 2 + 2 + (1 + (svc::kSubcodeBytes + 2) / 2) + 2;
constexpr std::uint32_t kFirstRoundWords = kSubcodeBracketWords
    + 2
    + 2
    + 1
    + proto::SpiFioQueue::kPieceWords;
static_assert(kSubcodeBracketWords == 56 && kFirstRoundWords == 453);
static_assert(
    (svc::kSubcodeBytes + 2) + 2 * svc::kRawFrameBytes <= proto::SpiFioQueue::kSlabBytes,
    "the primed pair and its subcode frame fit one act"
);

constexpr std::size_t kInlineFrameBytes = 128;
constexpr std::uint32_t kInlineFrameWords = 2 + 2 + 1 + kInlineFrameBytes + 2;

consteval bool cd_split_fits_every_board() {
    return hal::every_measured(&hal::BoardProfile::timing, [](const hal::LinkTiming& g) consteval {
        const auto t = hal::LinkTimingValues::measured(g);
        return kFirstRoundWords <= t.round_budget_words
            && t.round_cost(t.round_budget_words + kInlineFrameWords)
            < std::chrono::nanoseconds{reactor::kTickNs};
    });
}

static_assert(
    cd_split_fits_every_board(),
    "the tick round drains the subcode bracket and the first piece together, and an "
    "inline frame beside a full drain stays under 1 ms"
);

constexpr std::size_t kStatusCommandWords = 1 + CdEngine::kMaxStatusWords;
static_assert(
    CdEngine::kMaxStatusWords == 6,
    "UIO_CD_SET frames one opcode word plus the engine's six-word status ceiling"
);

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
        buf[used + 0] = static_cast<std::uint8_t>(n);
        buf[used + 1] = static_cast<std::uint8_t>((n >> 8));
        buf[used + 2] = static_cast<std::uint8_t>((n >> 16));
        buf[used + 3] = static_cast<std::uint8_t>((n >> 24));
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
            proto::WideIoIndex::of(io_index),
            infra::byte_cast<const std::uint8_t>(payload)
        };
        if (q->append(std::span(&b, 1))) {
            if (cdda_cap_ && cd_.io.cdda == io_index)
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
    if (cdda_cap_ && cd_.io.cdda == io_index) {
        cdda_cap_->append(payload);
    }
    auto ds = proto::ImageBracket::open(sink, proto::WideIoIndex::of(io_index));
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

Ex<void> CdWirePort::submit_sector(const CdSectorAct& act) {
    TASTY_SEAT_BODY(CdWirePort);
    const auto q = fio_queue_;
    if (!q)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), act.index.v});
    if (cdda_cap_ && cd_.io.cdda == act.index)
        cdda_cap_->append(std::as_bytes(act.sector));
    std::array<proto::SpiFioQueue::Bracket, 2> b{};
    std::size_t n = 0;
    if (!act.subcode.empty())
        b[n++] = {proto::WideIoIndex::of(act.sub_index), act.subcode};
    b[n++] = {proto::WideIoIndex::of(act.index), act.sector};
    return q->submit(std::span(b.data(), n));
}

bool CdWirePort::can_send_data(cdwire::Selector selector) {
    TASTY_SEAT_BODY(CdWirePort);
    hal::ISpiTransport& link = link_;
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(hal::SpiWord{cdwire::kUioCdGet}); !r) {
        ++wire_faults_;
        return false;
    }
    if (auto r = link.transfer(selector.word()); !r) {
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

    if (n > CdEngine::kMaxStatusWords)
        return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});

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

std::optional<CdCommandWords> CdWirePort::read_command_edge(CdEngine& eng) noexcept {
    TASTY_SEAT_BODY(CdWirePort);
    const std::size_t nwords = eng.command_word_count();

    CdCommandWords out{};
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
            const auto req = static_cast<std::uint8_t>(first->v);
            wanted = eng.on_command_edge(req);
            if (const auto sel = eng.command_selector(); wanted && sel) {

                if (auto r = link.transfer(sel->word()); !r) {
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
                    out.words[i] = r->v;
                }
            }
        }
    }

    if (!wanted || failed)
        return std::nullopt;
    out.count = nwords;
    return out;
}

void CdWirePort::declare_resets(const proto::ResetTerms& terms) noexcept {
    if (fio_queue_)
        fio_queue_->declare_resets(terms);
}

}
