// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class IBoardReset {
public:
    virtual ~IBoardReset() = default;
    virtual void board_reset() = 0;

protected:
    IBoardReset() = default;
    IBoardReset(const IBoardReset&) = default;
    IBoardReset& operator=(const IBoardReset&) = default;
};

class IRtPriorityScope {
public:
    virtual ~IRtPriorityScope() = default;
    virtual void relax() = 0;
    virtual void restore() = 0;

protected:
    IRtPriorityScope() = default;
    IRtPriorityScope(const IRtPriorityScope&) = default;
    IRtPriorityScope& operator=(const IRtPriorityScope&) = default;
};

class ISleeper {
public:
    virtual ~ISleeper() = default;
    virtual void sleep_ms(unsigned ms) = 0;

protected:
    ISleeper() = default;
    ISleeper(const ISleeper&) = default;
    ISleeper& operator=(const ISleeper&) = default;
};

class IStopSignal {
public:
    virtual ~IStopSignal() = default;
    virtual bool stop_requested() const = 0;

protected:
    IStopSignal() = default;
    IStopSignal(const IStopSignal&) = default;
    IStopSignal& operator=(const IStopSignal&) = default;
};

struct BoardOps {
    IBoardReset* reset = nullptr;
    ISleeper* sleeper = nullptr;
    IRtPriorityScope* rt = nullptr;
    IStopSignal* stop = nullptr;
};

}  // namespace mister::app
