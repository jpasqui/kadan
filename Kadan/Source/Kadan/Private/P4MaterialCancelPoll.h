#pragma once

namespace P4MaterialCancel
{
// Run-owned, game-thread gate for FSlowTask::ShouldCancel: that call also draws
// UI. Use a monotonic clock, and leave a work interval AFTER the draw returns.
// This gate does not throttle source validation or any material processing.
class FPostCallbackPoll
{
    bool Polled = false, Stopped = false;
    double NextPoll = 0.0;

public:
    template <class Clock, class PollUI>
    bool ShouldCancel(bool& Cancelled, Clock&& Now, PollUI&& UpdateAndCheckCancel)
    {
        // External cancellation and a previously observed UI cancel never wait
        // for the clock, and remain sticky even if the caller clears its flag.
        if (Cancelled || Stopped)
        {
            Cancelled = Stopped = true;
            return true;
        }
        if (Polled && Now() < NextPoll)
            return false;
        if (UpdateAndCheckCancel() || Cancelled)
        {
            Cancelled = Stopped = true;
            return true;
        }
        NextPoll = Now() + 0.2;
        Polled = true;
        return false;
    }
};
} //namespace P4MaterialCancel
