#if P4_MAPKIT_TESTS
// An option of one of the game's gameplay objects, changed in an imported Cave000, reaches the game through the
// object's row: export gives the row the game's blob with that one field changed (placement_changes "set").
#include "P4ExportDriverTestShared.h"

// Cave000_F00 already imported in the project (a copy of a kept fixture): the game's pot GCrackPotS2 with its option
// CrackPotAIParameter.bSendBreakEvent turned the other way, and nothing else changed. The pot's row of
// AP_Cave000_F00_P_Objects gets an AI.Static blob that is the game's with only that field changed; the pot is not
// moved, no level change names it, and no lighting is rebuilt.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameRowOptionsCave000Test, "P4MapKit.Export.GameRowOptionsCave000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameRowOptionsCave000Test::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    namespace P = P4MapKit::Placement;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty() || FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")).IsEmpty())
    { AddWarning(TEXT("P4MapKit test not run. ") TEXT("GameRowOptionsCave000 was not exercised. Set P4_NATIVE_ROOT and P4_NATIVE_ORIGINAL_CONTAINERS.")); return true; }
    if (!FPackageName::DoesPackageExist(Cave000))
    { AddWarning(TEXT("P4MapKit test not run. ") TEXT("GameRowOptionsCave000 needs a project with Cave000 imported (kadan_fixture.py copy cave000).")); return true; }
    FP4ExportModRequest Request; FP4ModNames Names; FString Error;
    if (!P4ExportDriverTest::Request(*this, TEXT("kadan.potoption"), TEXT("Cave000 pot option"), Request, Names)) return false;
    FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CAVE_GAME"));
    if (Game.IsEmpty()) Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    if (!TestTrue(TEXT("The test's input is there: ") + Game, IFileManager::Get().DirectoryExists(*Game))) return false;
    Request.ExtractedGameDirectory = Game;
    UWorld* Persistent = UEditorLoadingAndSavingUtils::LoadMap(Cave000);
    if (!TestTrue(TEXT("the map opens in the editor"), Persistent != nullptr)) return false;
    ULevel* Objects = nullptr; FString ObjectsPackage;
    for (ULevelStreaming* Streaming : Persistent->GetStreamingLevels())
        if (Streaming && Streaming->GetWorldAssetPackageName().EndsWith(TEXT("/Cave000_F00_Objects")))
        { Objects = Streaming->GetLoadedLevel(); ObjectsPackage = Streaming->GetWorldAssetPackageName(); }
    if (!TestTrue(TEXT("the object sublevel is loaded"), Objects != nullptr)) return false;
    AActor* Pot = nullptr;
    for (AActor* Actor : Objects->Actors)
        if (Actor && Actor->GetName() == TEXT("GCrackPotS2")) Pot = Actor;
    if (!TestTrue(TEXT("GCrackPotS2 is in the object sublevel"), Pot != nullptr)) return false;
    UWorld* ObjectsWorld = CastChecked<UWorld>(Objects->GetOuter());
    bool PotChangedBefore = false;
    // A fixture kept with an older import can read as changed already; the pot must not be among those changes.
    {
        const FP4LevelChangesResult Before = P4AnalyzeOriginalLevelChanges(ObjectsWorld);
        if (!TestTrue(TEXT("the level's changes can be read before the edit"), Before.IsValid())) return false;
        for (const FP4LevelActorChanges& One : Before.OtherChanged) AddInfo(TEXT("changed before the edit: ") + One.Actor);
        for (const FP4LevelActorMove& One : Before.Moved) AddInfo(TEXT("moved before the edit: ") + One.Actor);
        PotChangedBefore = Before.OtherChanged.ContainsByPredicate([](const FP4LevelActorChanges& One) { return One.Actor == TEXT("GCrackPotS2"); });
    }
    UActorComponent* PotAI = nullptr;
    for (UActorComponent* Component : Pot->GetComponents())
        for (UClass* Class = Component ? Component->GetClass() : nullptr; Class && !PotAI; Class = Class->GetSuperClass())
            if (Class->GetName() == TEXT("CrackPotAIComponent")) PotAI = Component;
    FStructProperty* Parameter = PotAI ? CastField<FStructProperty>(PotAI->GetClass()->FindPropertyByName(TEXT("CrackPotAIParameter"))) : nullptr;
    FBoolProperty* Send = Parameter ? CastField<FBoolProperty>(Parameter->Struct->FindPropertyByName(TEXT("bSendBreakEvent"))) : nullptr;
    if (!TestTrue(TEXT("GCrackPotS2 has CrackPotAIParameter.bSendBreakEvent on its AI component"), Send != nullptr)) return false;
    void* Value = Send->ContainerPtrToValuePtr<void>(Parameter->ContainerPtrToValuePtr<void>(PotAI));
    const bool WasSending = Send->GetPropertyValue(Value);
    {
        GEditor->BeginTransaction(FText::FromString(TEXT("Kadan game row option test")));
        PotAI->Modify();
        Send->SetPropertyValue(Value, !WasSending);
        GEditor->EndTransaction();
    }
    bool Undone = false;
    const auto Undo = [&]() { if (Undone) return; Undone = true; GEditor->UndoTransaction(); Objects->GetOutermost()->SetDirtyFlag(false); };
    ON_SCOPE_EXIT { Undo(); };
    const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(ObjectsWorld);
    const FP4LevelActorChanges* PotChanges = Changes.IsValid() ? Changes.OtherChanged.FindByPredicate([](const FP4LevelActorChanges& One)
        { return One.Actor == TEXT("GCrackPotS2"); }) : nullptr;
    if (!TestTrue(TEXT("the edit reads as a change of GCrackPotS2"), PotChanges != nullptr)) return false;
    for (const FString& Line : PotChanges->Lines.After) AddInfo(TEXT("line: ") + Line);
    {
        TArray<FP4GameRowEdit> Rows; bool Relight = true;
        if (TestTrue(TEXT("the review finds the pot's row: ") + Error, P4ExportLevelRowEdits(ObjectsWorld, Changes, Game, Rows, Relight, Error)))
        {
            const FP4GameRowEdit* PotRow = Rows.FindByPredicate([](const FP4GameRowEdit& Row) { return Row.Actor == TEXT("GCrackPotS2"); });
            TestTrue(TEXT("the pot's row is given AI.Static"), PotRow && PotRow->Id == UINT64_C(17179869271) && !PotRow->Moved && !PotRow->Removed &&
                PotRow->Options == TArray<FString>({TEXT("AI.Static")}));
            AddInfo(Relight ? TEXT("the review rebuilds the lighting") : TEXT("the review keeps the lighting"));
        }
    }
    Request.MapPackage = Cave000;
    FP4ExportRoot One; One.Path = ObjectsPackage; Request.Roots.Add(One);
    FP4ExportModResult Result;
    const bool Published = P4ExportMod(Request, Result, Error);
    for (const FString& Line : Result.Refused) AddInfo(TEXT("refused: ") + Line);
    for (const FString& Line : Result.Told) AddInfo(TEXT("told: ") + Line);
    if (!TestTrue(TEXT("the export publishes the edit"), Published)) { AddError(Error); return false; }
    TestTrue(TEXT("the export tells which row the pot's options went to"), Result.Told.ContainsByPredicate([](const FString& Line)
        { return Line.Contains(TEXT("GCrackPotS2 is given your options (AI.Static) by a change to row 17179869271")); }));
    if (!PotChangedBefore)
        TestFalse(TEXT("every change of the pot reaches the game"), Result.Told.ContainsByPredicate([](const FString& Line)
            { return Line.Contains(TEXT("GCrackPotS2 of")) && Line.Contains(TEXT("does not reach the game")); }));
    const FString List = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/ActorPlacementInfo/AP_Cave000_F00_P_Objects");
    const FString Folder = Result.Directory / TEXT("romfs/tsuchi/mods") / Request.Mod.Id;
    const TSharedPtr<FJsonObject> Manifest = ReadJson(Folder / TEXT("mod.json"));
    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr; const TSharedPtr<FJsonObject>* Entry = nullptr;
    const TSharedPtr<FJsonObject>* All = nullptr; const TSharedPtr<FJsonObject>* Rows = nullptr;
    if (!TestTrue(TEXT("mod.json has maps[].placement_changes for the objects list"), Manifest.IsValid() && Manifest->TryGetArrayField(TEXT("maps"), Maps) &&
        Maps->Num() == 1 && (*Maps)[0]->TryGetObject(Entry) && (*Entry)->TryGetObjectField(TEXT("placement_changes"), All) &&
        (*All)->Values.Num() == 1 && (*All)->TryGetObjectField(List, Rows))) return false;
    const TSharedPtr<FJsonObject>* Moves = nullptr; const TArray<TSharedPtr<FJsonValue>>* Removes = nullptr;
    TestFalse(TEXT("the pot is not moved or removed"), ((*Rows)->TryGetObjectField(TEXT("move"), Moves) && (*Moves)->HasField(TEXT("17179869271"))) ||
        ((*Rows)->TryGetArrayField(TEXT("remove"), Removes) && Removes->ContainsByPredicate([](const TSharedPtr<FJsonValue>& V) { return V->AsString() == TEXT("17179869271"); })));
    FString ChangesFile;
    if ((*Entry)->TryGetStringField(TEXT("changes"), ChangesFile))
    {
        FString Text; FFileHelper::LoadFileToString(Text, *(Folder / ChangesFile));
        AddInfo(TEXT("changes file: ") + Text.Left(400));
    }
    const TSharedPtr<FJsonObject>* Given = nullptr; const TSharedPtr<FJsonObject>* Blobs = nullptr; FString Hex;
    TArray<FP4GamePlacementRow> GameRows; FString Unread;
    const FString ListFolder = P4GamePlacementFolder(Game, ObjectsPackage);
    const FP4GamePlacementRow* GameRow = !ListFolder.IsEmpty() && P4ReadGamePlacementLists(ListFolder, GameRows, Unread)
        ? GameRows.FindByPredicate([](const FP4GamePlacementRow& Row) { return Row.Id == UINT64_C(17179869271); }) : nullptr;
    if (TestTrue(TEXT("the pot's row is given one blob, AI.Static"), (*Rows)->TryGetObjectField(TEXT("set"), Given) &&
            (*Given)->TryGetObjectField(TEXT("17179869271"), Blobs) && (*Blobs)->Values.Num() == 1 && (*Blobs)->TryGetStringField(TEXT("AI.Static"), Hex)) &&
        TestTrue(TEXT("the game's row of the pot is read: ") + Unread, GameRow && GameRow->Blobs.Contains(TEXT("AI.Static"))))
    {
        std::vector<uint8> Written;
        for (int32 At = 0; At + 1 < Hex.Len(); At += 2) Written.push_back(uint8(FParse::HexDigit(Hex[At]) << 4 | FParse::HexDigit(Hex[At + 1])));
        const TArray<uint8>& Bytes = GameRow->Blobs[TEXT("AI.Static")];
        P::Context PotChain; PotChain.ComponentChain = {"CrackPotAIComponent", "ObjectAIComponent", "CarrotAIComponent"};
        P::Blob Before, After; std::string Why;
        if (TestTrue(TEXT("both blobs read whole"), P::Decode(P::Slot::AIStatic, PotChain, std::vector<uint8>(Bytes.GetData(), Bytes.GetData() + Bytes.Num()), GameRow->AssetVersion, Before, Why) &&
                P::Decode(P::Slot::AIStatic, PotChain, Written, GameRow->AssetVersion, After, Why) && Before.Exact() && After.Exact()))
        {
            std::vector<std::string> Was, Is, Differ;
            P::Flatten(Before.Root, "", Was); P::Flatten(After.Root, "", Is);
            for (size_t Line = 0; Line < Was.size() && Line < Is.size(); ++Line) if (Was[Line] != Is[Line]) Differ.push_back(Is[Line]);
            for (const std::string& Line : Differ) AddInfo(TEXT("changed: ") + FString(UTF8_TO_TCHAR(Line.c_str())));
            const std::string Flipped = std::string("CrackPotAIParameter.bSendBreakEvent=") + (WasSending ? "false" : "true");
            TestTrue(TEXT("the blob has bSendBreakEvent the other way"), Was.size() == Is.size() && std::find(Differ.begin(), Differ.end(), Flipped) != Differ.end());
            // A fixture whose import reads the pot as changed already can carry other differences; a clean one may not.
            if (!PotChangedBefore)
                TestEqual(TEXT("the blobs differ in bSendBreakEvent alone"), int32(Differ.size()), 1);
        }
    }
    Undo();
    return !HasAnyErrors();
}
#endif
