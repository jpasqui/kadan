#include "P4GameLink.h"
#include "P4Common.h"
#include "HAL/PlatformTime.h"
#include "IPAddress.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
constexpr uint32 Loopback = 0x7f000001;

FString Encode(const TSharedRef<FJsonObject>& Message)
{
    FString Out = P4CondensedJson(Message);
    return Out;
}

TArray<TSharedPtr<FJsonValue>> Values(const TArray<TSharedPtr<FJsonValue>>& Args)
{
    return Args;
}
} //namespace

FP4GameLink::FP4GameLink(int32 InPort) : Port(InPort)
{}
FP4GameLink::~FP4GameLink()
{
    Close();
}

bool FP4GameLink::Open(FString& Error)
{
    if (Socket)
        return true;
    ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!Sockets)
    {
        Error = TEXT("This editor has no network sockets.");
        return false;
    }
    Socket = Sockets->CreateSocket(NAME_DGram, TEXT("Kadan game connection"), FNetworkProtocolTypes::IPv4);
    if (!Socket)
    {
        Error = TEXT("Kadan could not open a network socket.");
        return false;
    }
    const TSharedRef<FInternetAddr> Local = Sockets->CreateInternetAddr();
    Local->SetIp(Loopback);
    Local->SetPort(0);
    if (!Socket->SetNonBlocking(true) || !Socket->Bind(*Local))
    {
        Close();
        Error = TEXT("Kadan could not open a network socket on this PC.");
        return false;
    }
    Server = Sockets->CreateInternetAddr();
    Server->SetIp(Loopback);
    Server->SetPort(Port);
    return true;
}

void FP4GameLink::Close()
{
    if (Socket)
    {
        Socket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
        Socket = nullptr;
    }
    // Waiting requests are dropped unanswered: their owner is closing the link, often while being destroyed.
    Pending.Reset();
}

double FP4GameLink::SecondsSinceReply() const
{
    return LastReply > 0 ? FPlatformTime::Seconds() - LastReply : -1;
}

void FP4GameLink::Send(const TSharedRef<FJsonObject>& Message, FP4GameCallback Done)
{
    FP4GameReply Failed;
    Failed.Error = TEXT("closed");
    if (!Socket)
    {
        if (Done)
            Done(Failed);
        return;
    }
    const int64 Id = NextId++;
    Message->SetNumberField(TEXT("id"), double(Id));
    const FTCHARToUTF8 Bytes(*Encode(Message));
    int32 Sent = 0;
    if (!Socket->SendTo(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Sent, *Server) ||
        Sent != Bytes.Length())
    {
        Failed.Error = TEXT("send");
        if (Done)
            Done(Failed);
        return;
    }
    FPending& Item = Pending.Add(Id);
    Item.Done = MoveTemp(Done);
    Item.Sent = FPlatformTime::Seconds();
}

void FP4GameLink::Hello(FP4GameCallback Done)
{
    const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
    Message->SetBoolField(TEXT("hello"), true);
    Send(Message, MoveTemp(Done));
}

void FP4GameLink::Call(
    const FString& Name, const TArray<TSharedPtr<FJsonValue>>& Args, FP4GameCallback Done, bool bThisWorld)
{
    const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
    Message->SetStringField(TEXT("call"), Name);
    Message->SetArrayField(TEXT("args"), Values(Args));
    if (bThisWorld && Generation >= 0)
        Message->SetNumberField(TEXT("gen"), double(Generation));
    Send(Message, MoveTemp(Done));
}

void FP4GameLink::Batch(
    const TArray<TPair<FString, TArray<TSharedPtr<FJsonValue>>>>& Calls, FP4GameCallback Done, bool bThisWorld)
{
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const auto& Item : Calls)
    {
        const TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
        One->SetStringField(TEXT("call"), Item.Key);
        One->SetArrayField(TEXT("args"), Item.Value);
        Items.Add(MakeShared<FJsonValueObject>(One));
    }
    const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
    Message->SetArrayField(TEXT("batch"), Items);
    if (bThisWorld && Generation >= 0)
        Message->SetNumberField(TEXT("gen"), double(Generation));
    Send(Message, MoveTemp(Done));
}

void FP4GameLink::Eval(const FString& Source, FP4GameCallback Done)
{
    const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
    Message->SetStringField(TEXT("eval"), Source);
    Send(Message, MoveTemp(Done));
}

void FP4GameLink::Watch(const FString& Event, FP4GameCallback Done)
{
    const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
    Message->SetStringField(TEXT("watch"), Event);
    Send(Message, MoveTemp(Done));
}

void FP4GameLink::Unwatch(const FString& Event, FP4GameCallback Done)
{
    const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
    Message->SetStringField(TEXT("unwatch"), Event);
    Send(Message, MoveTemp(Done));
}

void FP4GameLink::Answer(const TSharedPtr<FJsonObject>& Reply)
{
    double IdNumber = 0;
    if (!Reply)
        return;
    FString Event;
    if (Reply->TryGetStringField(TEXT("event"), Event))
    {
        const TSharedPtr<FJsonObject>* Data = nullptr;
        Reply->TryGetObjectField(TEXT("data"), Data);
        if (OnEvent)
            OnEvent(Event, Data ? *Data : MakeShared<FJsonObject>());
        return;
    }
    if (!Reply->TryGetNumberField(TEXT("id"), IdNumber))
        return;
    const int64 Id = int64(IdNumber);
    FPending* Item = Pending.Find(Id);
    if (!Item)
        return; // a late answer to a request that already timed out
    if (Reply->HasField(TEXT("part")))
    {
        const int32 Part = int32(Reply->GetNumberField(TEXT("part"))), Of = int32(Reply->GetNumberField(TEXT("of")));
        Item->Parts.Add(Part, Reply->GetStringField(TEXT("text")));
        if (Item->Parts.Num() < Of)
            return;
        FString Joined;
        for (int32 Index = 1; Index <= Of; ++Index)
            Joined += Item->Parts.FindRef(Index);
        const TSharedPtr<FJsonObject> Whole = P4ParseJsonObject(Joined);
        if (!Whole)
            return;
        Answer(Whole);
        return;
    }
    FP4GameReply Result;
    Result.Whole = Reply;
    Result.bOk = Reply->GetBoolField(TEXT("ok"));
    Reply->TryGetStringField(TEXT("error"), Result.Error);
    Result.Value = Reply->TryGetField(TEXT("value"));
    double Gen = -1;
    if (Reply->TryGetNumberField(TEXT("gen"), Gen))
        Generation = Result.Generation = int64(Gen);
    LastReply = FPlatformTime::Seconds();
    FP4GameCallback Done = MoveTemp(Item->Done);
    Pending.Remove(Id);
    if (Done)
        Done(Result);
}

void FP4GameLink::Poll()
{
    if (!Socket)
        return;
    uint32 Size = 0;
    TArray<uint8> Data;
    const TSharedRef<FInternetAddr> From = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
    while (Socket && Socket->HasPendingData(Size))
    {
        Data.SetNumUninitialized(FMath::Max<uint32>(Size, 1));
        int32 Read = 0;
        if (!Socket->RecvFrom(Data.GetData(), Data.Num(), Read, *From))
            break;
        uint32 Ip = 0;
        From->GetIp(Ip);
        if (Ip != Loopback || Read <= 0)
            continue;
        const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Data.GetData()), Read);
        Answer(P4ParseJsonObject(FString(Text.Length(), Text.Get())));
    }
    const double Now = FPlatformTime::Seconds();
    TArray<int64> Late;
    for (const auto& Item : Pending)
        if (Now - Item.Value.Sent > Timeout)
            Late.Add(Item.Key);
    for (const int64 Id : Late)
    {
        FP4GameCallback Done = MoveTemp(Pending[Id].Done);
        Pending.Remove(Id);
        FP4GameReply Result;
        Result.bTimedOut = true;
        Result.Error = TEXT("no answer");
        if (Done)
            Done(Result);
    }
}

TSharedPtr<FJsonValue> FP4GameLink::Text(const FString& Value)
{
    return MakeShared<FJsonValueString>(Value);
}
TSharedPtr<FJsonValue> FP4GameLink::Number(double Value)
{
    return MakeShared<FJsonValueNumber>(Value);
}
TSharedPtr<FJsonValue> FP4GameLink::Placement(uint64 Value)
{
    return Text(FString::Printf(TEXT("%llu"), Value));
}

TSharedPtr<FJsonValue> FP4GameLink::Location(const FVector& Value)
{
    const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetNumberField(TEXT("x"), Value.X);
    Out->SetNumberField(TEXT("y"), Value.Y);
    Out->SetNumberField(TEXT("z"), Value.Z);
    return MakeShared<FJsonValueObject>(Out);
}

TSharedPtr<FJsonValue> FP4GameLink::Rotation(const FRotator& Value)
{
    const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetNumberField(TEXT("pitch"), Value.Pitch);
    Out->SetNumberField(TEXT("yaw"), Value.Yaw);
    Out->SetNumberField(TEXT("roll"), Value.Roll);
    return MakeShared<FJsonValueObject>(Out);
}

TSharedPtr<FJsonValue> FP4GameLink::Pose(const FVector& InLocation, const FRotator& InRotation)
{
    const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetField(TEXT("location"), Location(InLocation));
    Out->SetField(TEXT("rotation"), Rotation(InRotation));
    return MakeShared<FJsonValueObject>(Out);
}

bool FP4GameLink::ReadPose(const TSharedPtr<FJsonValue>& Value, FVector& OutLocation, FRotator& OutRotation)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Value || !Value->TryGetObject(Object))
        return false;
    const TSharedPtr<FJsonObject>* At = nullptr;
    const TSharedPtr<FJsonObject>* Turn = nullptr;
    if (!(*Object)->TryGetObjectField(TEXT("location"), At) || !(*Object)->TryGetObjectField(TEXT("rotation"), Turn))
        return false;
    OutLocation = FVector(
        (*At)->GetNumberField(TEXT("x")), (*At)->GetNumberField(TEXT("y")), (*At)->GetNumberField(TEXT("z")));
    OutRotation = FRotator((*Turn)->GetNumberField(TEXT("pitch")), (*Turn)->GetNumberField(TEXT("yaw")),
        (*Turn)->GetNumberField(TEXT("roll")));
    return true;
}

bool FP4GameLink::ReadPlacement(const TSharedPtr<FJsonValue>& Value, uint64& Out)
{
    if (!Value)
        return false;
    FString Digits;
    if (Value->TryGetString(Digits))
    {
        Out = FCString::Strtoui64(*Digits, nullptr, 10);
        return !Digits.IsEmpty();
    }
    double Number = 0;
    if (!Value->TryGetNumber(Number) || Number < 0)
        return false;
    Out = uint64(Number);
    return true;
}
