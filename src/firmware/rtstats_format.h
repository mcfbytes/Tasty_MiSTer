// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/thread_map.h"
#include "rt_evidence.h"

#define TASTY_RTSTATS_FMT \
    "{\"t\":\"rtstats\",\"seq\":%u,\"reason\":\"%s\"," \
    "\"heartbeat\":%llu,\"drains\":%llu,\"rss_bytes\":%s," \
    "\"hb_stalls\":%u,\"rss_over\":%u," \
    "\"ring_drops\":[%u,%u,%u,%u],\"spin_timeouts\":%u,\"spur\":%u," \
    "\"ev_depth\":%u,\"ev_lost\":%u,\"ev_lost_edge\":%u," \
    "\"fifo\":{\"reads\":%u,\"lines\":%u,\"unrouted\":%u,\"unrec\":%u}," \
    "\"ladder\":{\"live\":%u,\"pc\":%u,\"rungs\":%u,\"bytes\":%s," \
    "\"assets\":%u,\"bios\":%u,\"disc\":%u,\"save\":%u}," \
    "\"video\":{\"edges\":%u,\"staged\":%u,\"emitted\":%u,\"arm\":%u," \
    "\"refused\":%u,\"rf_bits\":%u,\"mode\":%u,\"fpix\":%u," \
    "\"fb\":{\"ack\":%u,\"en\":%u},\"vi\":%u,\"res\":%u," \
    "\"w\":%u,\"h\":%u," \
    "\"geo\":{\"s\":%u,\"c\":%u,\"r\":%u,\"e\":%u,\"l\":%u," \
    "\"g\":%u,\"t\":%u}," \
    "\"pre\":{\"a\":%u,\"d\":%u,\"ch\":%u,\"cw\":%u,\"pk\":%u," \
    "\"bt\":%u,\"er\":%u,\"ff\":%u," \
    "\"hi\":%u,\"gc\":%u,\"af\":%u,\"ak\":%u,\"tf\":%u,\"st\":%u}," \
    "\"i2c\":{\"o\":\"%s\",\"bus\":%u,\"w\":%u,\"e\":%u,\"reg\":%u," \
    "\"fo\":%u,\"fr\":%u,\"fe\":%u,\"le\":%u,\"ve\":%u,\"pe\":%u," \
    "\"rp\":%u,\"tr\":%u,\"bk\":%u}," \
    "\"spd\":{\"s\":%u,\"dis\":%u,\"dv\":%u,\"nn\":%u,\"reg\":%u}}," \
    "\"input\":{\"rounds\":%u,\"evts\":%u,\"keys\":%u,\"kdrop\":%u," \
    "\"sweep\":%u,\"rel\":%u,\"mouse\":%u,\"kick\":%u," \
    "\"reb\":%u,\"rref\":%u,\"evict\":%u,\"efail\":%u," \
    "\"rterr\":%u,\"dev\":%u,\"fds\":%u,\"map\":%u,\"slot\":%u," \
    "\"gate\":%u,\"mchg\":%u,\"jmax\":%u,\"jp\":%u,\"aedge\":%u," \
    "\"mrem\":%u,\"kovf\":%u,\"uik\":%u,\"uikd\":%u," \
    "\"qdrop\":%u,\"rej\":%u," \
    "\"btn\":%u,\"btnq\":%u," \
    "\"pl\":{\"live\":%s,\"gh\":%u," \
    "\"k\":[%s,%s,%s,%s,%s,%s]}}," \
    "\"mgl\":{\"fi\":%u,\"ab\":%u,\"arm\":%u,\"pub\":%u,\"r0\":%u}," \
    "\"ftx\":{\"n\":%u,\"b\":%s,\"idx\":%u," \
    "\"win\":%u,\"wref\":%u,\"wec\":%u}," \
    "\"blk\":{\"rd\":%u,\"srv\":%u,\"err\":%u,\"ec\":%u," \
    "\"ovs\":%u,\"unc\":%u,\"blank\":%u,\"wf\":%u,\"leg\":%u," \
    "\"cfg\":%u,\"dw\":%u,\"def\":%u,\"exp\":%u,\"sec\":%u," \
    "\"pfx\":%u,\"stg\":%u,\"stale\":%u}," \
    "\"db\":{\"dec\":%u,\"bnd\":%u,\"ref\":%u,\"fb\":%u," \
    "\"ret\":%u,\"drf\":%u}," \
    "\"rt\":{\"cpu\":%u,\"cov\":%u,\"ok\":%llu,\"nm\":%u,\"err\":%u}," \
    "\"rnd\":{\"ep\":%u,\"ems\":%u," \
    "\"sn\":%u,\"smn\":%u,\"smx\":%u,\"sp\":%u,\"so\":%u," \
    "\"sc\":%u,\"si\":%u," \
    "\"ln\":%u,\"lmx\":%u,\"lp\":%u," \
    "\"lc\":%u,\"li\":%u,\"lk\":%u," \
    "\"wn\":%u,\"wa\":%u,\"wmx\":%u,\"wp\":%u,\"tov\":%u," \
    "\"asn\":%u,\"asx\":%u,\"asc\":%u,\"asi\":%u,\"ase\":%u,\"aso\":%u," \
    "\"aln\":%u,\"alx\":%u,\"alc\":%u,\"ali\":%u,\"alk\":%u," \
    "\"awx\":%u}," \
    "\"cd\":{\"dlm\":%u,\"dls\":%u,\"pf\":%u,\"hit\":%u,\"miss\":%u," \
    "\"dec\":%u,\"seek\":%u,\"drop\":%u,\"err\":%u," \
    "\"esd\":%u,\"epk\":%u,\"eref\":%u," \
    "\"wait\":%u,\"sent\":%u," \
    "\"dsec\":%u,\"idle\":%u,\"gate\":%u," \
    "\"st\":%u,\"trk\":%u,\"lba\":%d," \
    "\"dat\":%u,\"alba\":%d," \
    "\"sub\":%u,\"ssub\":%u,\"nores\":%u,\"dus\":%u,\"ev\":%u," \
    "\"bsy\":%u,\"eab\":%u}," \
    "\"ui\":{\"depth\":%u,\"top\":%u,\"vis\":%u,\"cellref\":%u," \
    "\"pageref\":%u,\"bev\":%u,\"bedge\":%u,\"blng\":%u,\"bcls\":%u," \
    "\"binj\":%u,\"bcrs\":%u,\"bhold\":%u}," \
    "\"frame\":{\"seq\":%u}," \
    "\"shot\":{\"drops\":%u,\"rdrops\":%u,\"ok\":%u,\"fail\":%u," \
    "\"cap\":%u,\"ref\":%u,\"torn\":%u}," \
    "\"diag\":{\"drops\":%u,\"trunc\":%u}}"

#define TASTY_RTSTATS_U8_MAX 255u
#define TASTY_RTSTATS_U16_MAX 65535u
#define TASTY_RTSTATS_U32_MAX 4294967295u
#define TASTY_RTSTATS_U64_MAX 18446744073709551615ull
#define TASTY_RTSTATS_BOOL_MAX 1u

#define TASTY_RTSTATS_I32_WIDEST (-2147483647 - 1)

namespace mister::fw {

inline constexpr const char* kRtstatsReasonWorstCase = "alarm";

inline constexpr const char* kFileSizeWorstCase = "18446744073709551615";

}

#define TASTY_RTSTATS_WORST_ARGS \
    TASTY_RTSTATS_U32_MAX, ::mister::fw::kRtstatsReasonWorstCase, TASTY_RTSTATS_U64_MAX, \
        TASTY_RTSTATS_U64_MAX, ::mister::fw::kFileSizeWorstCase, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, 256u, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U8_MAX, TASTY_RTSTATS_U8_MAX, TASTY_RTSTATS_U32_MAX, \
        ::mister::fw::kFileSizeWorstCase, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_BOOL_MAX, \
        TASTY_RTSTATS_BOOL_MAX, TASTY_RTSTATS_BOOL_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, 511u, TASTY_RTSTATS_U8_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U16_MAX, TASTY_RTSTATS_BOOL_MAX, TASTY_RTSTATS_BOOL_MAX, \
        TASTY_RTSTATS_U16_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U16_MAX, \
        TASTY_RTSTATS_U16_MAX, TASTY_RTSTATS_U16_MAX, TASTY_RTSTATS_U16_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U16_MAX, TASTY_RTSTATS_U8_MAX, "configured", \
        TASTY_RTSTATS_U8_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U8_MAX, \
        TASTY_RTSTATS_U8_MAX, TASTY_RTSTATS_U8_MAX, TASTY_RTSTATS_U16_MAX, TASTY_RTSTATS_U16_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_BOOL_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U8_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, "63", 6u, \
        "4294967295", "4294967295", "4294967295", "4294967295", "4294967295", "4294967295", \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        ::mister::fw::kFileSizeWorstCase, TASTY_RTSTATS_U16_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U16_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U16_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_BOOL_MAX, \
        static_cast<unsigned long long>(::mister::fw::kRtEvidenceAllApplied), \
        (1u << ::mister::hal::kThreadSeats) - 1u, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, 536871u, TASTY_RTSTATS_U32_MAX, 4u, 23u, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, 536871u, 4u, 23u, 7u, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, 536871u, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, 4u, 23u, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, 4u, 23u, 7u, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, 16777215u, 2u, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, 255u, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_I32_WIDEST, 1u, TASTY_RTSTATS_I32_WIDEST, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, 41u, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, \
        TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX, TASTY_RTSTATS_U32_MAX
