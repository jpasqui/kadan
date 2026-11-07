#include "P4GameLink.h"
#include "P4GameMirror.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Level.h"
#include "Engine/Selection.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IPAddress.h"
#include "Internationalization/Regex.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
// A stand-in for the Dev Server on this PC: answers hello, find_placed and a long call, and never answers "silent".
struct FFakeServer
{
    FSocket* Socket = nullptr;
    int32 Port = 0;
    int32 Answered = 0;
    TArray<FString> Evals;
    FString SpawnReply; // the Mirror test can deliver spawn before the first census
    FFakeServer()
    {
        ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        Socket = Sockets->CreateSocket(NAME_DGram, TEXT("Kadan fake dev server"), FNetworkProtocolTypes::IPv4);
        const TSharedRef<FInternetAddr> Local = Sockets->CreateInternetAddr();
        Local->SetIp(0x7f000001);
        Local->SetPort(0);
        Socket->SetNonBlocking(true);
        Socket->Bind(*Local);
        Port = Socket->GetPortNo();
    }
    ~FFakeServer()
    {
        Socket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
    }
    void Reply(const FString& Text, const FInternetAddr& To)
    {
        const FTCHARToUTF8 Bytes(*Text);
        int32 Sent = 0;
        Socket->SendTo(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Sent, To);
    }
    void Serve()
    {
        uint32 Size = 0;
        const TSharedRef<FInternetAddr> From = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
        while (Socket->HasPendingData(Size))
        {
            TArray<uint8> Data;
            Data.SetNumUninitialized(Size);
            int32 Read = 0;
            if (!Socket->RecvFrom(Data.GetData(), Data.Num(), Read, *From))
                return;
            const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Data.GetData()), Read);
            TSharedPtr<FJsonObject> Request;
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), Request);
            if (!Request)
                continue;
            const int64 Id = int64(Request->GetNumberField(TEXT("id")));
            const FString Call = Request->HasField(TEXT("call")) ? Request->GetStringField(TEXT("call")) : FString();
            ++Answered;
            if (Request->HasField(TEXT("eval")))
            {
                const FString Source = Request->GetStringField(TEXT("eval"));
                Evals.Add(Source);
                if (!SpawnReply.IsEmpty() && Source.Contains(TEXT("kadan_preview.ordinary_create(")))
                    Reply(
                        FString::Printf(TEXT("{\"id\":%lld,\"ok\":true,\"value\":\"%s\",\"gen\":4}"), Id, *SpawnReply),
                        *From);
                else
                    Reply(FString::Printf(TEXT("{\"id\":%lld,\"ok\":true,\"value\":true,\"gen\":4}"), Id), *From);
            }
            else if (Request->HasField(TEXT("watch")))
            {
                Reply(FString::Printf(TEXT("{\"id\":%lld,\"ok\":true,\"value\":\"watching\",\"gen\":4}"), Id), *From);
                Reply(TEXT("{\"event\":\"entity.spawned\",\"data\":{\"kind\":\"BABY\"},\"gen\":4}"), *From);
            }
            else if (Request->HasField(TEXT("hello")))
                Reply(
                    FString::Printf(
                        TEXT(
                            "{\"id\":%lld,\"ok\":true,\"value\":{\"world\":\"Cave000_F00_P\",\"control\":true},\"gen\":4}"),
                        Id),
                    *From);
            else if (Call == TEXT("entity.find_placed"))
            {
                const FString Placement = Request->GetArrayField(TEXT("args"))[0]->AsString();
                if (Placement == TEXT("4611686035607257178"))
                    Reply(
                        FString::Printf(TEXT("{\"id\":%lld,\"ok\":true,\"value\":\"@actor:9\",\"gen\":4}"), Id), *From);
                else
                    Reply(FString::Printf(TEXT("{\"id\":%lld,\"ok\":false,\"error\":\"not_found\",\"gen\":4}"), Id),
                        *From);
            }
            else if (Call == TEXT("entity.pose"))
            {
                // Sent in two parts, the way the server sends a long reply.
                const FString Whole = FString::Printf(
                    TEXT(
                        "{\"id\":%lld,\"ok\":true,\"value\":{\"location\":{\"x\":1.5,\"y\":-2,\"z\":3},\"rotation\":{\"pitch\":0,\"yaw\":90,\"roll\":0}},\"gen\":5}"),
                    Id);
                const int32 Half = Whole.Len() / 2;
                for (int32 Part = 1; Part <= 2; ++Part)
                {
                    const TSharedRef<FJsonObject> Piece = MakeShared<FJsonObject>();
                    Piece->SetNumberField(TEXT("id"), double(Id));
                    Piece->SetNumberField(TEXT("part"), Part);
                    Piece->SetNumberField(TEXT("of"), 2);
                    Piece->SetStringField(TEXT("text"), Part == 1 ? Whole.Left(Half) : Whole.Mid(Half));
                    FString Out;
                    FJsonSerializer::Serialize(Piece, TJsonWriterFactory<>::Create(&Out));
                    Reply(Out, *From);
                }
            }
        }
    }
};

template <typename FDone>
bool PumpUntil(FP4GameLink& Link, FFakeServer* Server, FDone Done, double Seconds)
{
    const double End = FPlatformTime::Seconds() + Seconds;
    while (FPlatformTime::Seconds() < End)
    {
        if (Server)
            Server->Serve();
        Link.Poll();
        if (Done())
            return true;
        FPlatformProcess::Sleep(0.01f);
    }
    return false;
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameLinkWireTest, "P4MapKit.GameLink.Wire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameLinkWireTest::RunTest(const FString&)
{
    FFakeServer Server;
    FP4GameLink Link(Server.Port);
    FString Error;
    if (!TestTrue(TEXT("the link opens"), Link.Open(Error)))
        return false;

    FString World;
    bool bHello = false;
    Link.Hello(
        [&](const FP4GameReply& Reply)
        {
            bHello = true;
            const TSharedPtr<FJsonObject>* Value = nullptr;
            if (Reply.bOk && Reply.Value->TryGetObject(Value))
                (*Value)->TryGetStringField(TEXT("world"), World);
        });
    TestTrue(TEXT("hello is answered"), PumpUntil(Link, &Server, [&] { return bHello; }, 2.0));
    TestEqual(TEXT("the world"), World, FString(TEXT("Cave000_F00_P")));
    TestEqual(TEXT("the generation"), Link.GetGeneration(), int64(4));

    FString Handle, Missing;
    int32 Answers = 0;
    Link.Call(TEXT("entity.find_placed"), {FP4GameLink::Placement(4611686035607257178ull)},
        [&](const FP4GameReply& Reply)
        {
            ++Answers;
            Reply.Value->TryGetString(Handle);
        });
    Link.Call(TEXT("entity.find_placed"), {FP4GameLink::Placement(17)},
        [&](const FP4GameReply& Reply)
        {
            ++Answers;
            Missing = Reply.Error;
        });
    TestTrue(TEXT("both calls are answered"), PumpUntil(Link, &Server, [&] { return Answers == 2; }, 2.0));
    TestEqual(TEXT("a placement beyond 2^53 crosses exactly"), Handle, FString(TEXT("@actor:9")));
    TestEqual(TEXT("a refusal carries the game's result"), Missing, FString(TEXT("not_found")));

    FVector At;
    FRotator Turn;
    bool bPose = false;
    Link.Call(TEXT("entity.pose"), {FP4GameLink::Text(Handle)},
        [&](const FP4GameReply& Reply) { bPose = Reply.bOk && FP4GameLink::ReadPose(Reply.Value, At, Turn); });
    TestTrue(TEXT("a reply in two parts is joined"), PumpUntil(Link, &Server, [&] { return bPose; }, 2.0));
    TestEqual(TEXT("its location"), At, FVector(1.5f, -2.f, 3.f));
    TestEqual(TEXT("its yaw"), Turn.Yaw, 90.f);
    TestEqual(TEXT("the generation follows the latest reply"), Link.GetGeneration(), int64(5));

    bool bTimedOut = false;
    Link.Timeout = 0.3;
    Link.Call(TEXT("silent"), {}, [&](const FP4GameReply& Reply) { bTimedOut = Reply.bTimedOut; });
    TestTrue(TEXT("an unanswered call times out"), PumpUntil(Link, &Server, [&] { return bTimedOut; }, 2.0));
    TestEqual(TEXT("nothing is left waiting"), Link.Waiting(), 0);

    FString EventName, EventKind;
    Link.OnEvent = [&](const FString& Name, const TSharedPtr<FJsonObject>& Data)
    {
        EventName = Name;
        Data->TryGetStringField(TEXT("kind"), EventKind);
    };
    bool bWatching = false;
    Link.Watch(TEXT("entity.spawned"), [&](const FP4GameReply& Reply) { bWatching = Reply.bOk; });
    TestTrue(TEXT("a watched event arrives"),
        PumpUntil(Link, &Server, [&] { return bWatching && !EventName.IsEmpty(); }, 2.0));
    TestEqual(TEXT("its name"), EventName, FString(TEXT("entity.spawned")));
    TestEqual(TEXT("its data"), EventKind, FString(TEXT("BABY")));

    uint64 Placement = 0;
    TestTrue(TEXT("placements read from text"),
        FP4GameLink::ReadPlacement(FP4GameLink::Placement(4611686035607257178ull), Placement));
    TestEqual(TEXT("exactly"), Placement, uint64(4611686035607257178ull));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameMirrorEditsTest, "P4MapKit.GameLink.MirrorEdits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameMirrorEditsTest::RunTest(const FString&)
{
    UPackage* Package = CreatePackage(*(TEXT("/Temp/KadanMirror_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false, NAME_None, Package);
    if (!TestNotNull(TEXT("An owned editor world exists"), World))
        return false;
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    FFakeServer Server;
    FP4GameLink Link(Server.Port);
    FString Error;
    if (!TestTrue(TEXT("The test link opens"), Link.Open(Error)))
        return false;
    FP4GameMirror Mirror(Link);
    AActor* Parent = World->SpawnActor<AStaticMeshActor>();
    Parent->SetActorLocationAndRotation(FVector(100, 200, 0), FRotator(0, 90, 0));
    Parent->SetActorScale3D(FVector(2, 3, 1));
    AActor* Original = World->SpawnActor<AStaticMeshActor>();
    Original->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);
    Original->SetFlags(RF_Transactional);
    const FVector OriginalAt = Original->GetActorLocation();
    ULevel* PreviousLevel = World->GetCurrentLevel();
    Mirror.Attach(World);
    FP4GameMirror::FEntry& Entry = Mirror.Entries.Add(TEXT("@actor:9"));
    Entry.Handle = TEXT("@actor:9");
    Entry.Kind = TEXT("MESH");
    Entry.ClassPath = TEXT("/Script/Engine.StaticMeshActor");
    Entry.LevelActor = Original;
    Package->SetDirtyFlag(false);
    Mirror.Spawn(Entry);
    AActor* Actor = Entry.Actor.Get();
    if (!TestNotNull(TEXT("A real actor is mirrored"), Actor))
        return false;
    bool bFoundByEditor = false;
    for (TActorIterator<AActor> It(World); It; ++It)
        if (*It == Actor)
            bFoundByEditor = true;
    TestTrue(TEXT("The editor can find the live actor through its normal iterator"), bFoundByEditor);
    TestTrue(TEXT("The live copy preserves world scale under a scaled parent"),
        Actor->GetActorScale3D().Equals(Original->GetActorScale3D(), 0.001f));
    TestTrue(TEXT("The live copy uses a transient attachment parent"),
        Actor->GetAttachParentActor() && Actor->GetAttachParentActor() != Parent &&
            Actor->GetAttachParentActor()->HasAnyFlags(RF_Transient));
    TestTrue(TEXT("The live copy lives in a separate transient level"),
        Actor->GetLevel() != Original->GetLevel() && Actor->GetLevel()->HasAnyFlags(RF_Transient));
    TestFalse(TEXT("The original actor is protected during preview"), Original->IsEditable());
    TestFalse(TEXT("Spawning leaves the map clean"), Package->IsDirty());
    TestNotNull(TEXT("The live copy has a native root component"), Actor->GetRootComponent());
    Actor->Modify();
    TestFalse(TEXT("Modifying the live actor and its native root leaves the map clean"), Package->IsDirty());
    Actor->SetActorLocation(FVector(50, 20, 10));
    Actor->Tags.Add(TEXT("KadanKeptTest"));
    GEditor->BroadcastOnActorMoved(Actor);
    TestTrue(TEXT("The real editor move delegate sends a request"),
        PumpUntil(Link, &Server, [&] { return Server.Evals.Num() == 1; }, 2.0));
    if (Server.Evals.Num())
    {
        TestTrue(TEXT("A live actor uses the game's warp"), Server.Evals[0].Contains(TEXT("entity.warp(dog or a")));
        const FRegexPattern PositionPattern(TEXT("x = ([-0-9.]+), y = ([-0-9.]+), z = ([-0-9.]+)"));
        FRegexMatcher Position(PositionPattern, Server.Evals[0]);
        const bool bPosition = Position.FindNext() &&
            FVector(FCString::Atof(*Position.GetCaptureGroup(1)), FCString::Atof(*Position.GetCaptureGroup(2)),
                FCString::Atof(*Position.GetCaptureGroup(3)))
                .Equals(FVector(50, 20, 10), 0.01f);
        TestTrue(TEXT("The request carries the viewport position"), bPosition);
    }
    TestFalse(TEXT("Dragging the live copy leaves the map clean"), Package->IsDirty());
    TestEqual(TEXT("Dragging does not change the original actor"), Original->GetActorLocation(), OriginalAt);
    FEditorDelegates::PreSaveWorld.Broadcast(0, World);
    TestTrue(TEXT("A map save sees the original's real editor flags"), Original->IsEditable());
    FEditorDelegates::PostSaveWorld.Broadcast(0, World, false);
    TestFalse(TEXT("A failed save restores preview protection"), Original->IsEditable());
    GEditor->GetSelectedActors()->Select(Actor, true);
    TestTrue(TEXT("A selected live counterpart can keep its simulation changes"), Mirror.CanKeepSimulationChanges());
    TestTrue(TEXT("Unreal's property copier keeps changes"), Mirror.KeepSimulationChanges() > 0);
    TestEqual(TEXT("Keep changes copies the transform to the original"), Original->GetActorLocation(),
        Actor->GetActorLocation());
    TestTrue(TEXT("Keeping changes intentionally dirties the map"), Package->IsDirty());
    GEditor->GetSelectedActors()->Deselect(Actor);
    GEditor->UndoTransaction();
    TestEqual(TEXT("One native Undo restores the original transform"), Original->GetActorLocation(), OriginalAt);
    TestFalse(TEXT("Undo while previewing keeps the original protected"), Original->IsEditable());
    GEditor->RedoTransaction();
    TestTrue(TEXT("Native Redo restores the kept actor property"), Original->Tags.Contains(TEXT("KadanKeptTest")));
    Package->SetDirtyFlag(true);
    Mirror.Clear();
    TestFalse(TEXT("Stop destroys the live actor"), IsValid(Actor));
    TestTrue(TEXT("Sync preserves preexisting unsaved edits"), Package->IsDirty());
    TestTrue(TEXT("Stop restores the original current level"), World->GetCurrentLevel() == PreviousLevel);
    TestTrue(TEXT("Stop restores original editability"), Original->IsEditable());
    GEditor->UndoTransaction();
    TestFalse(
        TEXT("Undo after Stop restores the original actor property"), Original->Tags.Contains(TEXT("KadanKeptTest")));
    TestTrue(TEXT("Undo after Stop leaves the original editable"), Original->IsEditable());
    GEditor->RedoTransaction();
    TestTrue(TEXT("Redo after Stop leaves the original editable"), Original->IsEditable());
    TestFalse(TEXT("Redo after Stop leaves the original visible"), Original->IsTemporarilyHiddenInEditor());
    Mirror.Attach(World);
    Package->SetDirtyFlag(false);
    AStaticMeshActor* Dropped = World->SpawnActor<AStaticMeshActor>();
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("An asset is available to drag in"), Mesh))
        return false;
    Dropped->GetStaticMeshComponent()->SetStaticMesh(Mesh);
    Dropped->SetActorLocation(FVector(120, 240, 60));
    Dropped->SetActorScale3D(FVector(2, 1, 3));
    Dropped->Modify();
    TestFalse(TEXT("Dragging a new asset during preview keeps the map clean until Keep"), Package->IsDirty());
    const FVector DroppedAt = Dropped->GetActorLocation();
    Mirror.Owner = TEXT("0123456789abcdef0123456789abcdef");
    Server.SpawnReply = TEXT("@actor:77");
    FP4GameMirror::FEntry* PendingDrop = Mirror.EntryOf(Dropped);
    if (!TestNotNull(TEXT("The dropped actor is awaiting game creation"), PendingDrop))
        return false;
    Mirror.SpawnPlaced(FString::Printf(TEXT("editor:%u"), Dropped->GetUniqueID()), *PendingDrop);
    TestTrue(TEXT("The spawn reply binds before a game census"),
        PumpUntil(Link, &Server, [&] { return Mirror.HandleOf(Dropped) == TEXT("@actor:77"); }, 2.0));
    Mirror.NextQuery = FPlatformTime::Seconds() + 1000;
    Mirror.Tick(FPlatformTime::Seconds());
    Mirror.Tick(FPlatformTime::Seconds() + 0.1);
    TestEqual(
        TEXT("Two preview ticks never ease a new drop from the world origin"), Dropped->GetActorLocation(), DroppedAt);
    Mirror.Owner.Empty();
    Server.SpawnReply.Empty();
    GEditor->GetSelectedActors()->Select(Dropped, true);
    TestTrue(TEXT("A newly dropped asset offers Keep Simulation Changes"), Mirror.CanKeepSimulationChanges());
    TestTrue(TEXT("Keep creates an editor counterpart for a newly dropped asset"), Mirror.KeepSimulationChanges() > 0);
    FP4GameMirror::FEntry* DroppedEntry = Mirror.EntryOf(Dropped);
    AStaticMeshActor* Kept = DroppedEntry ? Cast<AStaticMeshActor>(DroppedEntry->LevelActor.Get()) : nullptr;
    if (!TestNotNull(TEXT("The new asset has a normal editor actor"), Kept))
        return false;
    TestTrue(TEXT("The kept asset is in the original level"), Kept->GetLevel() == PreviousLevel);
    TestFalse(TEXT("The kept actor is persistent"), Kept->HasAnyFlags(RF_Transient));
    TestTrue(
        TEXT("Keep preserves the imported mesh reference"), Kept->GetStaticMeshComponent()->GetStaticMesh() == Mesh);
    TestTrue(TEXT("Keep preserves the dropped transform"),
        Kept->GetActorTransform().Equals(Dropped->GetActorTransform(), 0.001f));
    TestEqual(
        TEXT("Keep persists the intended drop placement after the spawn reply"), Kept->GetActorLocation(), DroppedAt);
    TestTrue(TEXT("Keeping a new actor intentionally dirties the map"), Package->IsDirty());
    GEditor->GetSelectedActors()->Deselect(Dropped);
    Mirror.Clear();
    TestFalse(TEXT("Stop removes the dropped live copy"), IsValid(Dropped));
    TestTrue(TEXT("Stop retains the kept editor actor"),
        IsValid(Kept) && Kept->IsEditable() && !Kept->IsTemporarilyHiddenInEditor());
    GEditor->UndoTransaction();
    TestFalse(
        TEXT("One Undo removes the newly kept actor"), IsValid(Kept) && World->PersistentLevel->Actors.Contains(Kept));
    GEditor->RedoTransaction();
    TestTrue(
        TEXT("Redo restores the newly kept actor"), IsValid(Kept) && World->PersistentLevel->Actors.Contains(Kept));

    const FVector CookedAt = Kept->GetActorLocation();
    Mirror.Attach(World);
    Package->SetDirtyFlag(false);
    TestTrue(TEXT("A cooked scratch mesh binds to its existing source"),
        Mirror.BindCookedActor(Kept, TEXT("@actor:44"), CookedAt, Kept->GetActorRotation()));
    FP4GameMirror::FEntry* CookedEntry = Mirror.Entries.Find(TEXT("@actor:44"));
    AStaticMeshActor* CookedCopy = CookedEntry ? Cast<AStaticMeshActor>(CookedEntry->Actor.Get()) : nullptr;
    if (!TestNotNull(TEXT("The cooked mesh has an editable live counterpart"), CookedCopy))
        return false;
    TestEqual(TEXT("A template-bound cooked copy starts at the exact nonzero source world location"),
        CookedCopy->GetActorLocation(), CookedAt);
    TestTrue(TEXT("A template-bound cooked copy preserves source world scale"),
        CookedCopy->GetActorScale3D().Equals(Kept->GetActorScale3D(), 0.001f));
    TestTrue(TEXT("The cooked counterpart is source-bound and not a newly spawned game actor"),
        CookedEntry->bCooked && !CookedEntry->bUserPlaced && !CookedEntry->bPendingSpawn &&
            CookedEntry->LevelActor.Get() == Kept);
    TestTrue(TEXT("The cooked counterpart preserves the authored asset reference"),
        CookedCopy->GetStaticMeshComponent()->GetStaticMesh() == Mesh);
    TestTrue(TEXT("The cooked counterpart hides its protected original"),
        Kept->IsTemporarilyHiddenInEditor() && !Kept->IsEditable());
    TestFalse(TEXT("Binding a cooked mesh keeps the source map clean"), Package->IsDirty());
    Server.Evals.Reset();
    CookedCopy->SetActorLocation(CookedAt + FVector(25, 0, 0));
    CookedCopy->SetActorScale3D(FVector(3, 2, 1));
    GEditor->BroadcastOnActorMoved(CookedCopy);
    TestTrue(TEXT("Moving a cooked mesh sends a native game request"),
        PumpUntil(Link, &Server, [&] { return Server.Evals.Num() == 1; }, 2.0));
    if (Server.Evals.Num())
        TestTrue(TEXT("A cooked actor moves directly without joining owned-drop cleanup"),
            Server.Evals[0].Contains(TEXT("actor.set_transform")) &&
                Server.Evals[0].Contains(TEXT("SetActorScale3D")) && !Server.Evals[0].Contains(TEXT("ordinary_move")));
    TestEqual(TEXT("Moving the cooked live copy leaves its original in place"), Kept->GetActorLocation(), CookedAt);
    GEditor->GetSelectedActors()->Select(CookedCopy, true);
    TestTrue(TEXT("Keep updates the existing cooked mesh source"), Mirror.KeepSimulationChanges() > 0);
    TestTrue(TEXT("Keep preserves the cooked counterpart's full transform"),
        Kept->GetActorTransform().Equals(CookedCopy->GetActorTransform(), 0.001f));
    GEditor->GetSelectedActors()->Deselect(CookedCopy);
    Mirror.Clear();
    Server.Serve();
    Link.Poll();
    TestFalse(TEXT("Stop removes the cooked editor counterpart"), IsValid(CookedCopy));
    TestTrue(TEXT("Stop leaves the source cooked mesh visible and editable"),
        IsValid(Kept) && Kept->IsEditable() && !Kept->IsTemporarilyHiddenInEditor());
    TestEqual(TEXT("Stop sends no destroy request for the cooked game actor"), Server.Evals.Num(), 1);
    Package->SetDirtyFlag(false);
    return !HasAnyErrors();
}

// Against the game itself: runs only with KADAN_GAME_LIVE=1 while the game runs in Eden with the Dev Server mod.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameLinkLiveTest, "P4MapKit.GameLink.Live",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameLinkLiveTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_GAME_LIVE")) != TEXT("1"))
    {
        AddInfo(TEXT("Skipped. Set KADAN_GAME_LIVE=1 while the game runs with the Dev Server mod."));
        return true;
    }
    const FString PortText = FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_GAME_PORT"));
    FP4GameLink Link(PortText.IsEmpty() ? 47720 : FCString::Atoi(*PortText));
    Link.Timeout = 5.0;
    FString Error;
    if (!TestTrue(TEXT("the link opens"), Link.Open(Error)))
        return false;
    FString World;
    bool bControl = false, bHello = false;
    Link.Hello(
        [&](const FP4GameReply& Reply)
        {
            bHello = Reply.bOk;
            const TSharedPtr<FJsonObject>* Value = nullptr;
            if (Reply.bOk && Reply.Value->TryGetObject(Value))
            {
                (*Value)->TryGetStringField(TEXT("world"), World);
                (*Value)->TryGetBoolField(TEXT("control"), bControl);
            }
        });
    if (!TestTrue(TEXT("the game answers hello"),
            PumpUntil(Link, nullptr, [&] { return bHello || !Link.Waiting(); }, 6.0) && bHello))
        return false;
    AddInfo(FString::Printf(
        TEXT("The game is in %s, control %d, generation %lld."), *World, bControl, Link.GetGeneration()));
    if (!bControl)
        return true;

    bool bPose = false, bDone = false;
    FVector At;
    FRotator Turn;
    Link.Call(TEXT("player.captain"), {FP4GameLink::Number(0)},
        [&](const FP4GameReply& Captain)
        {
            if (!Captain.bOk)
            {
                bDone = true;
                return;
            }
            Link.Call(TEXT("entity.pose"), {Captain.Value},
                [&](const FP4GameReply& Pose)
                {
                    bPose = Pose.bOk && FP4GameLink::ReadPose(Pose.Value, At, Turn);
                    bDone = true;
                });
        });
    PumpUntil(Link, nullptr, [&] { return bDone; }, 12.0);
    if (TestTrue(TEXT("the captain's place is read"), bPose))
        AddInfo(FString::Printf(TEXT("The captain is at %s facing yaw %.1f."), *At.ToString(), Turn.Yaw));

    double Sum = 0;
    FString Printed;
    bool bEval = false;
    Link.Eval(TEXT("print('in the game') return 1 + 2"),
        [&](const FP4GameReply& Reply)
        {
            bEval = Reply.bOk && Reply.Value && Reply.Value->TryGetNumber(Sum);
            const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
            if (Reply.Whole && Reply.Whole->TryGetArrayField(TEXT("printed"), Lines) && Lines->Num())
                Printed = (*Lines)[0]->AsString();
        });
    PumpUntil(Link, nullptr, [&] { return bEval || !Link.Waiting(); }, 6.0);
    TestTrue(TEXT("the console runs Lua in the game"), bEval && Sum == 3);
    TestEqual(TEXT("and gives back what it printed"), Printed, FString(TEXT("in the game")));

    int32 Steps = 0;
    Link.OnEvent = [&](const FString& Name, const TSharedPtr<FJsonObject>&)
    {
        if (Name == TEXT("time.step"))
            ++Steps;
    };
    Link.Watch(TEXT("time.step"), [](const FP4GameReply&) {});
    PumpUntil(Link, nullptr, [] { return false; }, 1.0);
    Link.Unwatch(TEXT("time.step"), [](const FP4GameReply&) {});
    PumpUntil(Link, nullptr, [&] { return !Link.Waiting(); }, 2.0);
    AddInfo(FString::Printf(TEXT("%d time.step events arrived in one second."), Steps));
    TestTrue(TEXT("watched events arrive from the game"), Steps > 10);
    return true;
}

#endif
