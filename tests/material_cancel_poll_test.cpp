#include "../Kadan/Source/Kadan/Private/P4MaterialCancelPoll.h"
#include <iostream>
#define CHECK(X) do { ++Checks; if (!(X)) { std::cerr << "failed line " << __LINE__ << ": " << #X << '\n'; return 1; } } while (false)
int main()
{
    int Checks = 0, Draws = 0, Reads = 0;
    double Time = 100.0;
    bool Cancelled = false, Clicked = false;
    auto Clock = [&]() { ++Reads; return Time; };
    auto UI = [&]() { ++Draws; Time += 0.6; return Clicked; };
    P4MaterialCancel::FPostCallbackPoll Gate;
    CHECK(!Gate.ShouldCancel(Cancelled, Clock, UI)); CHECK(Draws == 1);
    for (int I = 0; I != 1000; ++I) CHECK(!Gate.ShouldCancel(Cancelled, Clock, UI));
    CHECK(Draws == 1);
    Time += 0.199; CHECK(!Gate.ShouldCancel(Cancelled, Clock, UI)); CHECK(Draws == 1);
    Time = 100.6 + 0.2; CHECK(!Gate.ShouldCancel(Cancelled, Clock, UI)); CHECK(Draws == 2);
    Clicked = true; Time += 0.2; CHECK(Gate.ShouldCancel(Cancelled, Clock, UI)); CHECK(Cancelled);
    int BeforeReads = Reads, BeforeDraws = Draws; Cancelled = false;
    CHECK(Gate.ShouldCancel(Cancelled, Clock, UI)); CHECK(Cancelled); CHECK(Reads == BeforeReads); CHECK(Draws == BeforeDraws);
    P4MaterialCancel::FPostCallbackPoll Fresh;
    Cancelled = false; Clicked = false;
    CHECK(!Fresh.ShouldCancel(Cancelled, Clock, UI)); CHECK(Draws == BeforeDraws + 1);
    Cancelled = true; BeforeReads = Reads; BeforeDraws = Draws;
    CHECK(Fresh.ShouldCancel(Cancelled, Clock, UI)); CHECK(Reads == BeforeReads); CHECK(Draws == BeforeDraws);
    Cancelled = false; CHECK(Fresh.ShouldCancel(Cancelled, Clock, UI)); CHECK(Cancelled);
    P4MaterialCancel::FPostCallbackPoll DuringUI;
    Cancelled = false;
    CHECK(DuringUI.ShouldCancel(Cancelled, Clock, [&]() { Cancelled = true; return false; }));
    P4MaterialCancel::FPostCallbackPoll AlreadyStopped;
    BeforeReads = Reads; BeforeDraws = Draws;
    CHECK(AlreadyStopped.ShouldCancel(Cancelled, Clock, UI)); CHECK(Reads == BeforeReads); CHECK(Draws == BeforeDraws);
    std::cout << "PASS " << Checks << " cancellation-poll checks: slow UI, deadline, sticky/external cancellation and run isolation\n";
    return 0;
}
