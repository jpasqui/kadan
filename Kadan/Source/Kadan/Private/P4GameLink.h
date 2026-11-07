#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FSocket;
class FInternetAddr;

// The answer to one request to the Dev Server mod in the running game (modding/devserver/README.md).
struct FP4GameReply
{
    bool bOk = false;
    bool bTimedOut = false;
    FString Error;                      // the game's result name or the server's, when bOk is false
    TSharedPtr<FJsonValue> Value;       // what the call returned
    TSharedPtr<FJsonObject> Whole;      // the whole reply, for batches (`results`)
    int64 Generation = -1;              // the game's world generation when it answered
};
using FP4GameCallback = TFunction<void(const FP4GameReply&)>;
// An event the game sent for a watch: its name ("entity.spawned") and its fields.
using FP4GameEvent = TFunction<void(const FString&, const TSharedPtr<FJsonObject>&)>;

// Kadan's side of the Dev Server's wire: JSON over UDP to 127.0.0.1. Nothing blocks. Requests are answered
// from Poll, which the owner calls often (the panel on a timer, tests in a loop).
class KADAN_API FP4GameLink
{
public:
    explicit FP4GameLink(int32 InPort = 47720);
    ~FP4GameLink();
    bool Open(FString& Error);
    void Close();
    bool IsOpen() const { return Socket != nullptr; }
    int32 GetPort() const { return Port; }
    int64 GetGeneration() const { return Generation; }
    double SecondsSinceReply() const;

    void Hello(FP4GameCallback Done);
    // One API call, for example Call("entity.pose", {Handle}). With bThisWorld the game refuses it once the
    // world has changed since the last reply.
    void Call(const FString& Name, const TArray<TSharedPtr<FJsonValue>>& Args, FP4GameCallback Done, bool bThisWorld = false);
    void Batch(const TArray<TPair<FString, TArray<TSharedPtr<FJsonValue>>>>& Calls, FP4GameCallback Done, bool bThisWorld = false);
    // Runs Lua in the game (the server's console). The reply's Whole has `printed`, the lines it printed.
    void Eval(const FString& Source, FP4GameCallback Done);
    // Starts or stops the game sending an event. Events arrive through OnEvent while Poll runs.
    void Watch(const FString& Event, FP4GameCallback Done);
    void Unwatch(const FString& Event, FP4GameCallback Done);
    FP4GameEvent OnEvent;
    // Reads every waiting datagram, answers what arrived and times out what waited longer than Timeout seconds.
    void Poll();
    int32 Waiting() const { return Pending.Num(); }

    double Timeout = 2.0;

    // Wire helpers.
    static TSharedPtr<FJsonValue> Text(const FString& Value);
    static TSharedPtr<FJsonValue> Number(double Value);
    static TSharedPtr<FJsonValue> Placement(uint64 Value); // as text, since a double can't hold every placement
    static TSharedPtr<FJsonValue> Location(const FVector& Value);
    static TSharedPtr<FJsonValue> Rotation(const FRotator& Value);
    static TSharedPtr<FJsonValue> Pose(const FVector& Location, const FRotator& Rotation);
    static bool ReadPose(const TSharedPtr<FJsonValue>& Value, FVector& Location, FRotator& Rotation);
    static bool ReadPlacement(const TSharedPtr<FJsonValue>& Value, uint64& Placement);

private:
    struct FPending
    {
        FP4GameCallback Done;
        double Sent = 0;
        TMap<int32, FString> Parts;
    };
    void Send(const TSharedRef<FJsonObject>& Message, FP4GameCallback Done);
    void Answer(const TSharedPtr<FJsonObject>& Reply);

    int32 Port;
    FSocket* Socket = nullptr;
    TSharedPtr<FInternetAddr> Server;
    int64 NextId = 1;
    int64 Generation = -1;
    double LastReply = 0;
    TMap<int64, FPending> Pending;
};
