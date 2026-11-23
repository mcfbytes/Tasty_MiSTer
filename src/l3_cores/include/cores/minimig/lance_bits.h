// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/minimig/lance_word.h"

namespace mister::cores::minimig {

struct LanceBits {

    struct Csr0 {
        static constexpr LanceWord kErr{0x8000};
        static constexpr LanceWord kBabl{0x4000};
        static constexpr LanceWord kCerr{0x2000};
        static constexpr LanceWord kMiss{0x1000};
        static constexpr LanceWord kMerr{0x0800};
        static constexpr LanceWord kRint{0x0400};
        static constexpr LanceWord kTint{0x0200};
        static constexpr LanceWord kIdon{0x0100};
        static constexpr LanceWord kIntr{0x0080};
        static constexpr LanceWord kInea{0x0040};
        static constexpr LanceWord kRxon{0x0020};
        static constexpr LanceWord kTxon{0x0010};
        static constexpr LanceWord kTdmd{0x0008};
        static constexpr LanceWord kStop{0x0004};
        static constexpr LanceWord kStrt{0x0002};
        static constexpr LanceWord kInit{0x0001};

        static constexpr LanceWord kCommands = kInit.with(kStrt).with(kStop).with(kTdmd);
        static constexpr LanceWord
            kAcks = kIdon.with(kTint).with(kRint).with(kMerr).with(kMiss).with(kCerr).with(kBabl);
        static constexpr LanceWord
            kCauses = kBabl.with(kMiss).with(kMerr).with(kRint).with(kTint).with(kIdon);
        static constexpr LanceWord kRaised = kIntr.with(kInea);
    };

    struct Mode {
        static constexpr LanceWord kProm{0x8000};
        static constexpr LanceWord kColl{0x0010};
        static constexpr LanceWord kDtcr{0x0008};
        static constexpr LanceWord kLoop{0x0004};
        static constexpr LanceWord kDtx{0x0002};
        static constexpr LanceWord kDrx{0x0001};
    };

    struct Tmd1 {
        static constexpr LanceWord kOwn{0x8000};
        static constexpr LanceWord kErr{0x4000};
        static constexpr LanceWord kAddFcs{0x2000};
        static constexpr LanceWord kStp{0x0200};
        static constexpr LanceWord kEnp{0x0100};
    };

    struct Tmd3 {
        static constexpr LanceWord kBuff{0x8000};
        static constexpr LanceWord kUflo{0x4000};
        static constexpr LanceWord kRtry{0x0400};
    };

    struct Rmd1 {
        static constexpr LanceWord kOwn{0x8000};
        static constexpr LanceWord kOflo{0x1000};
        static constexpr LanceWord kBuff{0x0400};
        static constexpr LanceWord kStp{0x0200};
        static constexpr LanceWord kEnp{0x0100};
    };
};

}
