// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/save_state_attach.h"

#include <cerrno>
#include <utility>

#include "app/durable_write.h"
#include "app/event.h"
#include "svc/file.h"

namespace mister::app {

SaveStateAttach::SaveStateAttach(const Wiring& w) noexcept
    : asks_(w.asks),
      facts_cell_(w.facts),
      copy_(w.copy),
      toast_(w.toast),
      files_(w.vfs),
      io_map_(w.map),
      ui_map_(w.ui_map),
      chan_(w.io_wake, xthread::Polled{}),
      ui_(w.aperture,
          proto::SaveStateEngine::Environment{
              files_,
              w.clock,
              ui_map_,
              infra::OptRef<proto::SaveStateEngine::ISaveToast>{*this}
          }) {}

Ex<std::size_t> SaveStateAttach::Files::read_at(
    std::string_view path,
    std::uint64_t offset,
    std::span<std::byte> dst
) {
    auto f = vfs_.open(path, svc::OpenMode::Read);

    if (!f && f.error().code == Errc::os && f.error().detail == ENOENT) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), ENOENT});
    }
    if (!f)
        return std::unexpected(f.error());
    std::size_t got = 0;
    while (got < dst.size()) {
        auto n = (*f)->read_at(offset + got, dst.subspan(got));
        if (!n)
            return std::unexpected(n.error());
        if (*n == 0)
            break;
        got += *n;
    }
    return got;
}

Ex<void> SaveStateAttach::Files::write_all_sync(
    std::string_view path,
    std::span<const std::byte> src
) {
    return durable_write(vfs_, path, src);
}

void SaveStateAttach::start() noexcept {
    seat_assert<SeatTag::Ui>(ERR_SITE(), "SaveStateAttach::start off T-UI");
    facts_cell_.publish(ui_.facts);
}

void SaveStateAttach::tick() noexcept {
    seat_assert<SeatTag::Ui>(ERR_SITE(), "SaveStateAttach::tick off T-UI");
    if (ui_.out) {
        reap_();
        return;
    }
    if (const auto ask = asks_.pop()) {
        take_ask_(*ask);
        return;
    }
    if (ui_.facts.live)
        scan_();
}

void SaveStateAttach::take_ask_(const Ask& ask) noexcept {
    copy_.invalidate();
    ui_.held = ask.generation;
    ui_.facts.live = false;
    const proto::SaveStateEngine::SaveTarget target{
        ask.core_name.view(),
        ask.rom_path.view(),
        ask.arcade
    };
    if (!ui_.engine.arm(target) || !ui_.engine.window_valid(ask.decl)) {
        ui_.engine.disarm();
        answer_(false);
        return;
    }
    auto loan = chan_.acquire();
    if (!loan) {
        ui_.engine.disarm();
        answer_(false);
        return;
    }
    loan->decl = ask.decl;
    loan->core_name = ask.core_name;
    loan->rom_path = ask.rom_path;
    loan->arcade = ask.arcade;
    loan->ok = 0;
    loan->aborted = 0;
    loan->abort_at = ui_.abort_level;
    loan->err = Error{Errc::io, 0, 0};
    chan_.send(std::move(loan));
    ui_.out = true;
}

void SaveStateAttach::reap_() noexcept {
    auto done = chan_.reap();
    if (!done)
        return;
    ui_.out = false;
    bool attached = done.completed() && done->ok != 0 && !ui_.stale;
    if (done.completed() && done->aborted != 0)
        ++ui_.facts.prime_aborts;
    ui_.stale = false;

    if (attached) {
        attached = (ui_.engine.mapped() ? ui_.engine.rebind(done->decl)
                                        : ui_.engine.attach(done->decl))
                       .has_value();
    }
    if (!attached)
        ui_.engine.disarm();
    answer_(attached);
}

void SaveStateAttach::answer_(bool attached) noexcept {
    ui_.facts.live = attached;
    if (!attached)
        ++ui_.facts.refusals;
    ui_.facts.answered = ui_.held;
    ui_.facts.saves_written = ui_.engine.saves_written();
    ui_.facts.write_failures = ui_.engine.write_failures();
    facts_cell_.publish(ui_.facts);
}

void SaveStateAttach::scan_() noexcept {
    struct LowerOnExit {
        SaveStateAttach& self;

        ~LowerOnExit() {
            if (!self.ui_.raised)
                return;
            self.ui_.raised = false;
            self.copy_.publish(cores::SaveCopyLevel{});
        }
    };

    {
        const LowerOnExit lower{*this};
        (void)ui_.engine.poll();
    }
    if (ui_.engine.saves_written() == ui_.facts.saves_written
        && ui_.engine.write_failures() == ui_.facts.write_failures)
        return;
    ui_.facts.saves_written = ui_.engine.saves_written();
    ui_.facts.write_failures = ui_.engine.write_failures();
    facts_cell_.publish(ui_.facts);
}

void SaveStateAttach::on_save(unsigned) {
    seat_assert<SeatTag::Ui>(ERR_SITE(), "SaveStateAttach::on_save off T-UI");
    ui_.raised = true;
    copy_.publish(cores::SaveCopyLevel{.writing = true});
    if (!toast_)
        return;
    toast_->deliver(
        infra::make<Event>(
            Event::InfoRequest{.id = InfoId::SavingState},
            Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}
        )
    );
}

void SaveStateAttach::on_ui_pause() noexcept {
    seat_assert<SeatTag::Ui>(ERR_SITE(), "SaveStateAttach::on_ui_pause off T-UI");
    copy_.invalidate();
    ui_.raised = false;
    if (ui_.out) {
        ui_.stale = true;
        abort_.publish(++ui_.abort_level);
    }

    ui_.engine.disarm();
    (void)ui_.engine.detach();

    std::uint32_t dropped = 0;
    while (const auto ask = asks_.pop()) {
        ui_.held = ask->generation;
        ++dropped;
    }
    if (dropped == 0 && !ui_.facts.live)
        return;
    ui_.facts.live = false;
    ui_.facts.refusals += dropped;
    if (dropped != 0)
        ui_.facts.answered = ui_.held;
    facts_cell_.publish(ui_.facts);
}

void SaveStateAttach::finish_(bool ok, Error err) noexcept {
    io_.primer.end();
    io_.job->ok = ok ? 1u : 0u;
    io_.job->err = err;
    io_.job.complete();
    io_.job = Chan::Job{};
}

void SaveStateAttach::serve() noexcept {
    seat_assert<SeatTag::Io>(ERR_SITE(), "SaveStateAttach::serve off T-IO");
    const proto::SavePrimer::Env env{files_, io_map_};
    if (!io_.job) {
        io_.job = chan_.take();
        if (!io_.job)
            return;
        const PrimeOrder& order = *io_.job;
        const proto::SaveStateEngine::SaveTarget target{
            order.core_name.view(),
            order.rom_path.view(),
            order.arcade
        };
        if (auto begun = io_.primer.begin(env, target, order.decl); !begun)
            finish_(false, begun.error());
        return;
    }
    if (abort_.sample().value != io_.job->abort_at) {
        io_.job->aborted = 1;
        finish_(false, Error{Errc::cancelled, ERR_SITE(), 0});
        return;
    }
    auto more = io_.primer.step(env);
    if (!more) {
        finish_(false, more.error());
        return;
    }
    if (!*more)
        finish_(true, Error{Errc::io, 0, 0});
}

bool SaveStateAttach::idle() const noexcept {
    return !io_.job && chan_.outbound() == 0;
}

}
