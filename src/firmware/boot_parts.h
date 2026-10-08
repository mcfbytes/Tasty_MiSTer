// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "hal/bridge_sequencer.h"
#include "hal/doorbell_policy.h"
#include "hal/fpga_aperture.h"
#include "hal/fpga_programmer.h"
#include "hal/kernel_contract.h"
#include "hal/link_port.h"
#include "hal/link_timing.h"
#include "hal/phys_region.h"
#include "hal/program_geometry.h"
#include "hal/thread_map.h"
#include "hal/video_out_decl.h"
#include "infra/log_lane.h"
#include "infra/rt_stats.h"
#include "os/kernel_rt.h"
#include "os/types.h"
#include "reactor/executive.h"
#include "reactor/round_timer.h"
#include "svc/vfs.h"

namespace mister::fw {

struct RtEvidence;

struct BootParts {
    hal::ILinkPort& link;
    hal::IBridgeSequencer& bridges;
    hal::IFpgaProgrammer& programmer;
    hal::FpgaAperture fpga_mem;
    hal::PhysRegion lw_window;

    std::span<const hal::PhysRegion> regions;
    hal::DoorbellPolicy doorbells;
    os::UioLineSpace doorbell_nodes;
    hal::VideoOutDecl video;
    hal::KernelContract kernel;
    const hal::ThreadMap& threads;
    reactor::Executive& exec;
    xthread::RtStats& stats;

    reactor::RoundTimer& round_timer;
    xthread::LogLane& rt_lane;
    const svc::Vfs& vfs;

    RtEvidence& rt_evidence;

    os::KernelRt kernel_rt = os::KernelRt::Unknown;

    hal::LinkTimingValues link_timing;
    hal::ProgramGeometryValues program;
};

}  // namespace mister::fw
