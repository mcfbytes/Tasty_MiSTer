// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/file_stream_service.h"

#include "app/stream_ask_dispatch.h"

#include <cstring>
#include <span>
#include <utility>

namespace mister::app {
namespace {

struct IoState {
    StreamFile& src;
    StreamFile& out;
    std::uint32_t& gen;
    xthread::Counter& opens;
    xthread::Counter& closes;
    xthread::Counter& stales;
};

using Ask = FileStreamSlot::Ask;
using Job = FileStreamService::Chan::Job;

struct Performer {
    const svc::Vfs& vfs;
    IoState st;

    [[nodiscard]] bool stale_(const Ask::Head& h, Job& job) noexcept {
        if (h.gen == st.gen) return false;
        st.stales.add(1);
        job->err = Error{Errc::stale, ERR_SITE(), h.gen};
        return true;
    }

    void on(const Ask::Open& a, const Ask::Head& h, Job& job) noexcept {

        st.src.f.reset();
        st.src.size = 0;
        st.out.f.reset();
        st.out.size = 0;
        st.gen = h.gen;
        if (!a.rel.view().empty()) {
            auto r = stream_open(vfs, a.rel.view());
            if (!r) {
                job->err = r.error();
                return;
            }
            st.src = std::move(*r);
        }
        if (!a.out_rel.view().empty()) {
            auto w = stream_open(vfs, a.out_rel.view(), a.out_mode);
            if (!w) {

                st.src.f.reset();
                st.src.size = 0;
                job->err = w.error();
                return;
            }
            st.out = std::move(*w);
        }
        st.opens.add(1);
        job->size = st.src.size;
        job->ok = 1;
    }

    void on(const Ask::Read& a, const Ask::Head& h, Job& job) noexcept {
        if (stale_(h, job)) return;
        if (st.src.f == nullptr) {
            job->err = Error{Errc::not_found, ERR_SITE(), 0};
            return;
        }
        auto r = stream_read(*st.src.f, a.offset, std::span<std::byte>(job->data, a.want));
        if (!r) {
            job->err = r.error();
            return;
        }
        job->got = a.want;
        job->ok = 1;
    }

    void on(const Ask::Write& a, const Ask::Head& h, Job& job) noexcept {
        if (stale_(h, job)) return;
        if (st.out.f == nullptr) {
            job->err = Error{Errc::not_found, ERR_SITE(), 0};
            return;
        }
        auto r = stream_write(*st.out.f, a.offset, std::span<const std::byte>(job->data, a.want));
        if (!r) {
            job->err = r.error();
            return;
        }
        job->got = a.want;
        job->ok = 1;
    }

    void on(const Ask::Sync&, const Ask::Head& h, Job& job) noexcept {
        if (stale_(h, job)) return;
        if (st.out.f == nullptr) {
            job->err = Error{Errc::not_found, ERR_SITE(), 0};
            return;
        }
        if (auto r = stream_sync(*st.out.f); !r) {
            job->err = r.error();
            return;
        }
        job->ok = 1;
    }

    void on(const Ask::Close&, const Ask::Head&, Job& job) noexcept {
        if (st.src.f != nullptr || st.out.f != nullptr) st.closes.add(1);
        st.src.f.reset();
        st.src.size = 0;
        st.out.f.reset();
        st.out.size = 0;
        job->ok = 1;
    }

    void misrouted(const Ask&, Job& job) noexcept {
        job->err = Error{Errc::bad_format, ERR_SITE(), 0};
    }
};

struct OpenWatch {
    bool answered_ok;
    bool& open;
    void on(const Ask::Open&, const Ask::Head&) noexcept {
        if (!answered_ok) open = false;
    }
    void on(const Ask::Read&, const Ask::Head&) noexcept {}
    void on(const Ask::Write&, const Ask::Head&) noexcept {}
    void on(const Ask::Sync&, const Ask::Head&) noexcept {}
    void on(const Ask::Close&, const Ask::Head&) noexcept {}
    void misrouted(const Ask&) noexcept {}
};

void perform_stream(const svc::Vfs& vfs, IoState st, Job& job) noexcept {

    seat_assert<SeatTag::Io>(ERR_SITE(), "perform_stream off T-IO");
    job->ok = 0;
    Performer p{vfs, st};
    infra::dispatch<StreamAskRoutes>(job->ask, p, job);
    job.complete();
}
}  // namespace

FileStreamService::Loan FileStreamService::acquire_() noexcept {
    if (vfs_ == nullptr) return Loan{};
    auto loan = chan_.acquire();
    if (!loan) refusals_.add(1);
    return loan;
}

void FileStreamService::send_(Loan&& l) noexcept { chan_.send(std::move(l)); }

bool FileStreamService::arm_open(std::string_view rel) noexcept {
    return arm_open(rel, std::string_view{}, svc::OpenMode::Truncate);
}

bool FileStreamService::arm_open(std::string_view rel, std::string_view out_rel,
                                 svc::OpenMode out_mode) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "FileStreamService::arm_open off T-RT");

    if (rt_stream_open_ || (rel.empty() && out_rel.empty())) {
        refusals_.add(1);
        return false;
    }
    auto loan = acquire_();
    if (!loan) return false;
    Ask::Open a{};
    if (!a.rel.assign(rel) || !a.out_rel.assign(out_rel)) {
        refusals_.add(1);
        return false;
    }
    a.out_mode = out_mode;
    loan->ask = infra::make<Ask>(a, Ask::Head{++gen_});
    loan->size = 0;
    loan->got = 0;
    loan->ok = 0;
    rt_stream_open_ = true;
    send_(std::move(loan));
    return true;
}

bool FileStreamService::arm_step(std::uint64_t off, std::uint32_t want) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "FileStreamService::arm_step off T-RT");
    if (want == 0 || want > kStreamChunkBytes) {
        refusals_.add(1);
        return false;
    }
    auto loan = acquire_();
    if (!loan) return false;
    loan->ask = infra::make<Ask>(Ask::Read{.offset = off, .want = want}, Ask::Head{gen_});
    loan->got = 0;
    loan->ok = 0;
    send_(std::move(loan));
    return true;
}

bool FileStreamService::arm_write(std::uint64_t off, std::span<const std::byte> bytes) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "FileStreamService::arm_write off T-RT");
    if (bytes.empty() || bytes.size() > kStreamChunkBytes) {
        refusals_.add(1);
        return false;
    }
    auto loan = acquire_();
    if (!loan) return false;
    loan->ask = infra::make<Ask>(
        Ask::Write{.offset = off, .want = static_cast<std::uint32_t>(bytes.size())},
        Ask::Head{gen_});
    loan->got = 0;
    loan->ok = 0;
    std::memcpy(loan->data, bytes.data(), bytes.size());
    send_(std::move(loan));
    return true;
}

bool FileStreamService::arm_sync() noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "FileStreamService::arm_sync off T-RT");
    auto loan = acquire_();
    if (!loan) return false;
    loan->ask = infra::make<Ask>(Ask::Sync{}, Ask::Head{gen_});
    loan->got = 0;
    loan->ok = 0;
    send_(std::move(loan));
    return true;
}

bool FileStreamService::arm_close() noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "FileStreamService::arm_close off T-RT");
    auto loan = acquire_();
    if (!loan) return false;
    loan->ask = infra::make<Ask>(Ask::Close{}, Ask::Head{gen_});
    loan->got = 0;
    loan->ok = 0;
    rt_stream_open_ = false;
    send_(std::move(loan));
    return true;
}

FileStreamService::Loan FileStreamService::reap() noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "FileStreamService::reap off T-RT");
    auto loan = chan_.reap();

    if (loan) {
        OpenWatch watch{loan.completed() && loan->ok != 0, rt_stream_open_};
        infra::dispatch<StreamAskRoutes>(loan->ask, watch);
    }
    return loan;
}

void FileStreamService::serve() noexcept {
    if (vfs_ == nullptr) return;
    if (auto job = chan_.take())
        perform_stream(*vfs_, IoState{src_, out_, io_gen_, opens_, closes_, stales_}, job);
}

bool FileStreamService::idle() const noexcept { return chan_.outbound() == 0; }

void FileStreamService::pump_on_caller() noexcept {

    if (sealed_) {
        sealed_pumps_.add(1);
        return;
    }
    if (vfs_ == nullptr) return;
    const SeatScope stands_in_for_io{SeatTag::Io};
    for (std::size_t i = 0; i < kStreamDepth; ++i) {
        auto job = chan_.take();
        if (!job) break;
        perform_stream(*vfs_, IoState{src_, out_, io_gen_, opens_, closes_, stales_}, job);
    }
}

}  // namespace mister::app
