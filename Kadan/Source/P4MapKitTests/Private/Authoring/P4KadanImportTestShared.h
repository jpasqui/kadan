// The helpers the Kadan import tests share (P4KadanImportAutomation.cpp, P4KadanImportMapAutomation.cpp).
#pragma once
#include "P4KadanImport.h"
#include "P4KadanAssetPolicy.h"
#include "P4OriginalAssetImport.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "P4RecoveredFoliageType.h"
#include "P4ExportDriver.h"
#include "P4KadanContainerIndex.h"
#include "P4ExportProcess.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/blueprint_deep.h"
THIRD_PARTY_INCLUDES_END
#include "AssetRegistryModule.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectIterator.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "UObject/StructOnScope.h"

namespace
{
FString WaterboxDefaultMeasurements(const TCHAR* Stage)
{
    TArray<FString> Rows;
    for (const TCHAR* Name : {TEXT("TriggerContext"), TEXT("FollowTriggerContext"), TEXT("WaterboxFollowContext")})
    {
        UScriptStruct* Definition = FindObject<UScriptStruct>(nullptr, *(FString(TEXT("/Script/Carrot4.")) + Name));
        FBoolProperty* Check = Definition ? FindFProperty<FBoolProperty>(Definition, TEXT("bCheck")) : nullptr;
        if (!Check)
        {
            Rows.Add(FString::Printf(TEXT("%s standalone %s unavailable"), Stage, Name));
            continue;
        }
        FStructOnScope Value(Definition);
        Rows.Add(FString::Printf(TEXT("%s standalone %s bCheck=%d offset=%d size=%d native_ops=%d"), Stage, Name,
            int32(Check->GetPropertyValue_InContainer(Value.GetStructMemory())), Check->GetOffset_ForInternal(),
            Definition->GetStructureSize(), int32(Definition->GetCppStructOps() != nullptr)));
    }
    // Read the actual imported objects, their CDOs and their chosen archetypes.
    // A class default can differ from the independently constructed struct.
    for (TObjectIterator<UObject> It; It; ++It)
    {
        if (!IsValid(*It))
            continue;
        const FString ClassName = It->GetClass()->GetName();
        if (!ClassName.StartsWith(TEXT("GBaby")) && !ClassName.StartsWith(TEXT("GTekiBase")) &&
            !(ClassName == TEXT("GActor") && It->HasAnyFlags(RF_ClassDefaultObject)))
            continue;
        FStructProperty* Context = FindFProperty<FStructProperty>(It->GetClass(), TEXT("WaterBoxFollowContext"));
        FBoolProperty* Check = Context ? FindFProperty<FBoolProperty>(Context->Struct, TEXT("bCheck")) : nullptr;
        if (!Check)
            continue;
        const auto Read = [&](UObject* Object)
        {
            return Object ? int32(Check->GetPropertyValue_InContainer(Context->ContainerPtrToValuePtr<void>(Object)))
                          : -1;
        };
        Rows.Add(FString::Printf(TEXT("%s object %s bCheck=%d cdo=%d archetype=%d archetype_path=%s"), Stage,
            *It->GetPathName(), Read(*It), Read(It->GetClass()->GetDefaultObject(false)), Read(It->GetArchetype()),
            It->GetArchetype() ? *It->GetArchetype()->GetPathName() : TEXT("none")));
        if (Rows.Num() >= 512)
        {
            Rows.Add(TEXT("bounded object capture reached 512 rows"));
            break;
        }
    }
    Rows.Sort();
    return FString::Join(Rows, TEXT("\n")) + TEXT("\n");
}

// PRIVATE ImportMapCave000 observation; no creation, mutation or import replay.
struct FRow13CompilerCapture
{
    UEditorEngine* Editor = GEditor;
    FDelegateHandle Pre, Compiled, Reinstanced, Replaced, BeforeGC;
    TArray<FString> Rows;
    TSet<FString> SourcePlans, Pending;
    int32 Sequence = 0, Characters = 0;
    bool Bounded = false, SawRelevantCompile = false, FlushFailed = false;
    FString LastFamilyState;

    static bool RelevantName(const FString& Name)
    {
        return Name == TEXT("GTekiBase") || Name == TEXT("GBaby") || Name == TEXT("GCrackPotBase") ||
            Name == TEXT("GCrackPotS") || Name == TEXT("GAvatarBase") || Name == TEXT("GAvatar");
    }
    static bool Relevant(UBlueprint* Blueprint)
    {
        return IsValid(Blueprint) && RelevantName(Blueprint->GetName()) &&
            Blueprint->GetOutermost()->GetName().StartsWith(TEXT("/Game/Carrot4/"));
    }
    static FString Identity(UObject* Object)
    {
        return Object ? FString::Printf(TEXT("%s#%u"), *Object->GetPathName(), Object->GetUniqueID()) : TEXT("none");
    }
    static FString Context(UObject* Object)
    {
        FStructProperty* Field = Object
            ? FindFProperty<FStructProperty>(Object->GetClass(), TEXT("WaterBoxFollowContext"))
            : nullptr;
        FBoolProperty* Check = Field ? FindFProperty<FBoolProperty>(Field->Struct, TEXT("bCheck")) : nullptr;
        return Check
            ? FString::Printf(TEXT("context=%s offset=%d size=%d bCheck=%d native_ops=%d"),
                  *Field->Struct->GetPathName(), Check->GetOffset_ForInternal(), Field->Struct->GetStructureSize(),
                  int32(Check->GetPropertyValue_InContainer(Field->ContainerPtrToValuePtr<void>(Object))),
                  int32(Field->Struct->GetCppStructOps() != nullptr))
            : TEXT("context=unavailable");
    }
    void Add(const FString& Row)
    {
        if (Rows.Num() >= 2048 || Characters + Row.Len() > 512 * 1024)
        {
            Bounded = true;
            return;
        }
        Rows.Add(Row);
        Characters += Row.Len();
    }
    void Source(UBlueprint* Blueprint)
    {
        const FString Package = Blueprint->GetOutermost()->GetName();
        if (SourcePlans.Contains(Package))
            return;
        const FString File = FPaths::ProjectSavedDir() / TEXT("Kadan/BlueprintPlans/import") /
            (Package.Mid(1) + TEXT(".json"));
        FString Text;
        TSharedPtr<FJsonObject> Plan;
        const int64 Size = IFileManager::Get().FileSize(*File);
        if (Size < 0 || Size > 2 * 1024 * 1024 || !FFileHelper::LoadFileToString(Text, *File) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan) || !Plan.IsValid())
        {
            Add(TEXT("import-plan unavailable ") + Package);
            return;
        }
        SourcePlans.Add(Package);
        if (Blueprint->GetName() == TEXT("GAvatar") || Blueprint->GetName() == TEXT("GAvatarBase"))
            for (const TCHAR* Group : {TEXT("components"), TEXT("overrides")})
            {
                const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
                if (!Plan->TryGetArrayField(Group, Components))
                    continue;
                for (const auto& Component : *Components)
                {
                    if (!Component.IsValid() || Component->Type != EJson::Object)
                        continue;
                    const auto Record = Component->AsObject();
                    FString Variable;
                    const TArray<TSharedPtr<FJsonValue>>* Stored = nullptr;
                    if (!Record->TryGetStringField(TEXT("variable"), Variable) || Variable != TEXT("CakEventHandler") ||
                        !Record->TryGetArrayField(TEXT("values"), Stored))
                        continue;
                    for (const auto& Value : *Stored)
                    {
                        if (!Value.IsValid() || Value->Type != EJson::Object)
                            continue;
                        FString Name, PropertyText;
                        const auto Property = Value->AsObject();
                        if (Property->TryGetStringField(TEXT("name"), Name) && Name.StartsWith(TEXT("Unit_")))
                        {
                            Property->TryGetStringField(TEXT("text"), PropertyText);
                            Add(TEXT("import-plan ") + Package + TEXT(" ") + Group + TEXT(" CakEventHandler ") + Name +
                                TEXT("=") + PropertyText.Left(2048));
                        }
                    }
                }
            }
        const TSharedPtr<FJsonObject>* Defaults = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        bool Found = false;
        const bool HaveValues = Plan->TryGetObjectField(TEXT("class_defaults"), Defaults) && Defaults &&
            Defaults->IsValid() && (*Defaults)->TryGetArrayField(TEXT("values"), Values);
        if (HaveValues)
            for (const auto& Value : *Values)
            {
                if (!Value.IsValid() || Value->Type != EJson::Object)
                    continue;
                FString Name, Stored;
                const auto Record = Value->AsObject();
                if (Record->TryGetStringField(TEXT("name"), Name) &&
                    Name.Equals(TEXT("WaterBoxFollowContext"), ESearchCase::IgnoreCase))
                {
                    const bool HaveText = Record->TryGetStringField(TEXT("text"), Stored);
                    Found = true;
                    Add(TEXT("import-plan ") + Package + TEXT(" WaterBoxFollowContext text=") + Stored.Left(2048) +
                        (!HaveText                    ? TEXT(" [text unavailable]")
                                : Stored.Len() > 2048 ? TEXT(" [truncated]")
                                                      : TEXT("")));
                }
            }
        if (!HaveValues)
            Add(TEXT("import-plan ") + Package + TEXT(" class-default values unavailable"));
        else if (!Found)
            Add(TEXT("import-plan ") + Package + TEXT(" WaterBoxFollowContext property absent"));
        const TArray<TSharedPtr<FJsonValue>>* Overrides = nullptr;
        if (Plan->TryGetArrayField(TEXT("overrides"), Overrides))
            for (const auto& Override : *Overrides)
            {
                if (!Override.IsValid() || Override->Type != EJson::Object)
                    continue;
                FString Variable;
                const auto Record = Override->AsObject();
                if (!Record->TryGetStringField(TEXT("variable"), Variable) || Variable != TEXT("CarrotSkeletalMesh") ||
                    !Record->TryGetArrayField(TEXT("values"), Values))
                    continue;
                for (const auto& Value : *Values)
                {
                    if (!Value.IsValid() || Value->Type != EJson::Object)
                        continue;
                    FString Name, Stored;
                    const auto Property = Value->AsObject();
                    if (Property->TryGetStringField(TEXT("name"), Name) && Name == TEXT("AnimClass"))
                    {
                        Property->TryGetStringField(TEXT("text"), Stored);
                        Add(TEXT("import-plan ") + Package + TEXT(" CarrotSkeletalMesh override AnimClass=") +
                            Stored.Left(2048));
                    }
                }
            }
    }
    static FString ComponentState(UActorComponent* Template, const FComponentKey& Key)
    {
        FObjectPropertyBase* Animation = Template
            ? FindFProperty<FObjectPropertyBase>(Template->GetClass(), TEXT("AnimClass"))
            : nullptr;
        UObject* AnimClass = Animation ? Animation->GetObjectPropertyValue_InContainer(Template) : nullptr;
        FObjectPropertyBase* Mesh = Template
            ? FindFProperty<FObjectPropertyBase>(Template->GetClass(), TEXT("SkeletalMesh"))
            : nullptr;
        UObject* SkeletalMesh = Mesh ? Mesh->GetObjectPropertyValue_InContainer(Template) : nullptr;
        return TEXT("template=") + Identity(Template) + TEXT(" outer=") +
            Identity(Template ? Template->GetOuter() : nullptr) + TEXT(" class=") +
            Identity(Template ? Template->GetClass() : nullptr) + TEXT(" AnimClass=") + Identity(AnimClass) +
            TEXT(" SkeletalMesh=") + Identity(SkeletalMesh) + TEXT(" owner=") + Identity(Key.GetComponentOwner()) +
            TEXT(" variable=") + Key.GetSCSVariableName().ToString() + TEXT(" guid=") +
            Key.GetAssociatedGuid().ToString();
    }
    static void HandlerState(TArray<FString>& State, const FString& Label, UInheritableComponentHandler* Value)
    {
        State.Add(Label + TEXT(" handler=") + Identity(Value));
        if (!Value)
            return;
        for (auto It = Value->CreateRecordIterator(); It; ++It)
            if (It->ComponentKey.GetSCSVariableName() == TEXT("CarrotSkeletalMesh"))
                State.Add(Label + TEXT(" override ") + ComponentState(It->ComponentTemplate, It->ComponentKey));
    }
    static void AvatarUnits(TArray<FString>& State, const FString& Label, UActorComponent* Template, bool Parent,
        const TSet<UObject*>& Live, TSet<UActorComponent*>& Seen)
    {
        if (!Template || Seen.Contains(Template))
            return;
        Seen.Add(Template);
        // A stored field pointer is the evidence under examination. Only live
        // iterator objects and exact same-outer lookups may be dereferenced.
        if (!Live.Contains(Template))
        {
            State.Add(Label + FString::Printf(TEXT(" avatar_template=%p [not in live object iterator]"), Template));
            return;
        }
        State.Add(Label + TEXT(" avatar_template=") + Identity(Template) + TEXT(" outer=") +
            Identity(Template->GetOuter()) + TEXT(" class=") + Identity(Template->GetClass()) +
            FString::Printf(TEXT(" flags=0x%08x"), uint32(Template->GetFlags())));
        const TCHAR* Names[] = {TEXT("CakMoveSpeedControlUnit"), TEXT("CakFoliageDensityControlUnit"),
            TEXT("CakSinkRatioControlUnit"), TEXT("CakTerrainSwitchingUnit"), TEXT("CakControlledStatusControlUnit"),
            TEXT("CakStickyScrapeControlUnit"), TEXT("CakDamageLoopUnit")};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
        {
            const FString Name = FString::Printf(TEXT("Unit_%d"), Index);
            FObjectPropertyBase* Field = FindFProperty<FObjectPropertyBase>(Template->GetClass(), FName(*Name));
            UObject* Stored = Field ? Field->GetObjectPropertyValue_InContainer(Template) : nullptr;
            const FString ChildName = FString::Printf(TEXT("%s_%d"), Names[Index], int32(Parent && Index < 4));
            UObject* Child = StaticFindObjectFast(nullptr, Template, FName(*ChildName));
            const bool ChildLive = Child && Live.Contains(Child);
            FString Row = Label +
                FString::Printf(
                    TEXT(
                        " %s offset=%d property_flags=0x%llx raw=%p expected_child=%s lookup=%p match=%d child_live=%d"),
                    *Name, Field ? Field->GetOffset_ForInternal() : -1,
                    Field ? uint64(Field->GetPropertyFlags()) : uint64(0), Stored, *ChildName, Child,
                    int32(Stored && Stored == Child), int32(ChildLive));
            if (Field)
                Row += TEXT(" property_class=") + Identity(Field->PropertyClass);
            if (ChildLive)
                Row += TEXT(" child=") + Identity(Child) + TEXT(" child_class=") + Identity(Child->GetClass()) +
                    TEXT(" child_outer=") + Identity(Child->GetOuter()) +
                    FString::Printf(TEXT(" child_flags=0x%08x"), uint32(Child->GetFlags()));
            State.Add(Row); // Never call any UObject method through Stored.
        }
    }
    static FString FamilyState()
    {
        TArray<FString> State;
        TSet<UObject*> Live;
        for (TObjectIterator<UObject> It; It; ++It)
            if (IsValid(*It))
                Live.Add(*It);
        for (TObjectIterator<UBlueprint> It; It; ++It)
        {
            UBlueprint* Blueprint = *It;
            if (!Relevant(Blueprint))
                continue;
            UBlueprintGeneratedClass* Class = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
            UObject* CDO = Class ? Class->GetDefaultObject(false) : nullptr;
            UClass* Parent = Class ? Class->GetSuperClass() : nullptr;
            UObject* ParentCDO = Parent ? Parent->GetDefaultObject(false) : nullptr;
            const FString Label = TEXT("blueprint=") + Identity(Blueprint);
            State.Add(Label + TEXT(" class=") + Identity(Class) + TEXT(" cdo=") + Identity(CDO) + TEXT(" ") +
                Context(CDO) + TEXT(" parent=") + Identity(Parent) + TEXT(" parent_cdo=") + Identity(ParentCDO) +
                TEXT(" parent_") + Context(ParentCDO));
            HandlerState(State, Label, Blueprint->GetInheritableComponentHandler(false));
            HandlerState(State, Label + TEXT(" generated=") + Identity(Class),
                Class ? Class->GetInheritableComponentHandler(false) : nullptr);
            const bool Avatar = Blueprint->GetName() == TEXT("GAvatar") || Blueprint->GetName() == TEXT("GAvatarBase");
            const bool AvatarParent = Blueprint->GetName() == TEXT("GAvatarBase");
            TSet<UActorComponent*> Seen;
            if (Avatar)
                for (UInheritableComponentHandler* Handler : {Blueprint->GetInheritableComponentHandler(false),
                         Class ? Class->GetInheritableComponentHandler(false) : nullptr})
                    if (Handler)
                        for (auto Record = Handler->CreateRecordIterator(); Record; ++Record)
                            if (Record->ComponentKey.GetSCSVariableName() == TEXT("CakEventHandler"))
                                AvatarUnits(State, Label + TEXT(" override"), Record->ComponentTemplate, AvatarParent,
                                    Live, Seen);
            if (Blueprint->SimpleConstructionScript)
                for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
                {
                    if (Node && Node->GetVariableName() == TEXT("CarrotSkeletalMesh"))
                        State.Add(Label + TEXT(" scs ") + ComponentState(Node->ComponentTemplate, FComponentKey(Node)));
                    if (Avatar && Node && Node->GetVariableName() == TEXT("CakEventHandler"))
                        AvatarUnits(State, Label + TEXT(" scs"), Node->ComponentTemplate, AvatarParent, Live, Seen);
                }
        }
        State.Sort(); // Object-iterator order does not establish a state change.
        return FString::Join(State, TEXT("\n"));
    }
    void Snapshot(const FString& Callback, bool OnlyChanged = false)
    {
        if (Bounded)
            return;
        const FString Current = FamilyState();
        if (OnlyChanged && Current == LastFamilyState)
            return;
        LastFamilyState = Current;
        FString Phase, Text;
        TSharedPtr<FJsonObject> Report;
        const FString ReportFile = FPaths::ProjectSavedDir() / TEXT("Kadan/blueprint-import.json");
        const int64 ReportSize = IFileManager::Get().FileSize(*ReportFile);
        if (ReportSize >= 0 && ReportSize <= 2 * 1024 * 1024 && FFileHelper::LoadFileToString(Text, *ReportFile) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Report) && Report.IsValid())
            Report->TryGetStringField(TEXT("phase"), Phase);
        const FString Label = FString::Printf(
            TEXT("event=%d callback=%s import_phase=%s"), ++Sequence, *Callback, *Phase);
        Add(Label);
        for (TObjectIterator<UBlueprint> It; It; ++It)
            if (Relevant(*It))
                Source(*It);
        TArray<FString> State;
        Current.ParseIntoArrayLines(State);
        for (const FString& Row : State)
            Add(Label + TEXT(" ") + Row);
        if (!Callback.StartsWith(TEXT("engine-pre-garbage-collect")) && !Flush())
            FlushFailed = true;
    }
    static bool RelevantObject(UObject* Object)
    {
        if (!Object)
            return false;
        if (Object->GetOutermost()->GetName().StartsWith(TEXT("/Game/Carrot4/")) &&
            RelevantName(FPackageName::GetShortName(Object->GetOutermost()->GetName())))
            return true;
        for (UObject* Outer = Object; Outer; Outer = Outer->GetOuter())
            if (UClass* Class = Cast<UClass>(Outer))
                if (Relevant(Cast<UBlueprint>(Class->ClassGeneratedBy)))
                    return true;
        return false;
    }
    FRow13CompilerCapture()
    {
        if (!Editor)
            return;
        Pre = Editor->OnBlueprintPreCompile().AddLambda(
            [this](UBlueprint* Blueprint)
            {
                if (Relevant(Blueprint))
                {
                    SawRelevantCompile = true;
                    Pending.Add(Identity(Blueprint));
                    Snapshot(TEXT("engine-precompile ") + Identity(Blueprint));
                }
            });
        Compiled = Editor->OnBlueprintCompiled().AddLambda(
            [this]()
            {
                if (Pending.Num())
                {
                    Snapshot(TEXT("engine-compiled (batch; see precompile identities)"));
                    Pending.Empty();
                }
            });
        Reinstanced = Editor->OnBlueprintReinstanced().AddLambda(
            [this]()
            {
                if (SawRelevantCompile)
                    Snapshot(TEXT("engine-reinstanced (global callback; relevant state changed)"), true);
            });
        Replaced = Editor->OnObjectsReplaced().AddLambda(
            [this](const UEditorEngine::ReplacementObjectMap& Map)
            {
                bool RelevantReplacement = false;
                for (const auto& Pair : Map)
                    if (RelevantObject(Pair.Key) || RelevantObject(Pair.Value))
                    {
                        RelevantReplacement = true;
                        Add(TEXT("engine-objects-replaced old=") + Identity(Pair.Key) + TEXT(" ") + Context(Pair.Key) +
                            TEXT(" new=") + Identity(Pair.Value) + TEXT(" ") + Context(Pair.Value));
                    }
                if (RelevantReplacement)
                    Snapshot(TEXT("engine-objects-replaced relevant mapping"));
            });
        BeforeGC = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddLambda(
            [this]()
            {
                // Independent of the historical row bound: GC needs the current
                // live pointer comparison even when earlier callbacks filled Rows.
                if (!FlushCurrentPreGC())
                    FlushFailed = true;
                Snapshot(TEXT("engine-pre-garbage-collect"), true);
                Add(TEXT("engine-pre-garbage-collect flush_checkpoint"));
                // A fatal GC cannot reach Save(). Keep the last bounded observation
                // before traversal, without changing reachability or object values.
                if (!Flush())
                    FlushFailed = true;
            });
    }
    ~FRow13CompilerCapture()
    {
        if (!Editor)
            return;
        Editor->OnBlueprintPreCompile().Remove(Pre);
        Editor->OnBlueprintCompiled().Remove(Compiled);
        Editor->OnBlueprintReinstanced().Remove(Reinstanced);
        Editor->OnObjectsReplaced().Remove(Replaced);
        FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(BeforeGC);
    }
    bool FlushCurrentPreGC() const
    {
        const FString Current = FamilyState();
        const FTCHARToUTF8 UTF8(*Current);
        constexpr int32 Limit = 64 * 1024;
        // Reserve a bounded header and final newline; preserve a complete UTF-8
        // boundary if an unexpectedly large current state has to be truncated.
        int32 Count = FMath::Min(UTF8.Length(), Limit - 256);
        while (Count > 0 && Count < UTF8.Length() && (uint8(UTF8.Get()[Count]) & 0xc0) == 0x80)
            --Count;
        const FString Header = FString::Printf(
            TEXT(
                "callback=engine-pre-garbage-collect historic_event=%d historic_bounded=%d current_state_truncated=%d\n"),
            Sequence, int32(Bounded), int32(Count != UTF8.Length()));
        const FTCHARToUTF8 HeaderBytes(*Header);
        TArray<uint8> Bytes;
        Bytes.Append(reinterpret_cast<const uint8*>(HeaderBytes.Get()), HeaderBytes.Length());
        Bytes.Append(reinterpret_cast<const uint8*>(UTF8.Get()), Count);
        Bytes.Add(uint8('\n'));
        const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/RecoveryImports/Cave000PreGCState.txt");
        return Bytes.Num() <= Limit && IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) &&
            FFileHelper::SaveArrayToFile(Bytes, *File);
    }
    bool Flush() const
    {
        const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/RecoveryImports/Cave000CompilerStages.txt");
        return IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) &&
            FFileHelper::SaveStringToFile(
                FString::Join(Rows, TEXT("\n")) + FString::Printf(TEXT("\nbounded=%d\n"), int32(Bounded)), *File,
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    bool Save(FAutomationTestBase& Test)
    {
        Snapshot(TEXT("normal-import-returned"));
        const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/RecoveryImports/Cave000CompilerStages.txt");
        const bool Saved = Flush() && !FlushFailed;
        Test.AddInfo(TEXT("Private actual compiler callbacks: ") + File);
        return Test.TestTrue(TEXT("Keep bounded normal-import compiler callback observations"), Saved);
    }
};

bool KeepImportTestReport(FAutomationTestBase& Test, const TCHAR* Name, const FP4KadanImportResult& Result)
{
    // Existing normal importer output, retained before the next test reuses Saved/Kadan.
    const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/RecoveryImports") / (FString(Name) + TEXT(".json"));
    const FTCHARToUTF8 Bytes(*Result.Report);
    if (!Bytes.Length() || Bytes.Length() > 16 * 1024 * 1024 ||
        !IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) ||
        !FFileHelper::SaveStringToFile(Result.Report, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Test.AddError(TEXT("The actual import test report could not be retained: ") + File);
        return false;
    }
    Test.AddInfo(TEXT("Actual import report: ") + File);
    return true;
}
} //namespace
// Shared by these three development fixtures; compiled only into the test DLL.
namespace P4FixtureRetirement
{
TSet<FName> LivePackages();
bool Roots(const TArray<UObject*>& Owned, const TArray<UObject*>& Direct);
bool Package(UPackage* Made);
} //namespace P4FixtureRetirement

namespace P4KadanImportTests
{
inline TArray<FString> ContentFiles()
{
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(
        Files, *FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()), TEXT("*"), true, false);
    for (FString& File : Files)
        File = FPaths::ConvertRelativePathToFull(File);
    return Files;
}

// Takes what an import made out of the editor and off the disk again: its assets leave the asset
// registry, its worlds are destroyed, its packages are unloaded, and its files and the folders they
// leave empty are deleted.
inline bool Remove(const TArray<FString>& Files, const TSet<FName>& Preserve = {})
{
    TArray<UPackage*> Packages;
    for (const FString& File : Files)
    {
        FString Package;
        if (FPackageName::IsPackageFilename(File) && FPackageName::TryConvertFilenameToLongPackageName(File, Package))
            if (UPackage* Made = FindPackage(nullptr, *Package))
                Packages.AddUnique(Made);
    }
    bool Retired = true;
    TSet<FName> Kept = Preserve;
    for (UPackage* Made : Packages)
    {
        if (Preserve.Contains(Made->GetFName()))
            continue;
        if (!P4FixtureRetirement::Package(Made))
        {
            Kept.Add(Made->GetFName());
            Retired = false;
        }
    }
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
    for (const FString& File : Files)
    {
        FString Name;
        if (FPackageName::TryConvertFilenameToLongPackageName(FPaths::ChangeExtension(File, TEXT("uasset")), Name) &&
            Kept.Contains(FName(*Name)))
            continue;
        if (IFileManager::Get().FileExists(*File) && !IFileManager::Get().Delete(*File, false, true, true))
            Retired = false;
        for (FString Folder = FPaths::GetPath(File);
            FPaths::IsUnderDirectory(Folder, Content) && !FPaths::IsSamePath(Folder, Content);
            Folder = FPaths::GetPath(Folder))
            if (!IFileManager::Get().DeleteDirectory(*Folder, false, false))
                break;
    }
    return Retired;
}

// The project's files before a test imports are kept in a file of their own while it runs, so that a
// run that was cut off is cleaned up by the next one.
struct FMade
{
    FString List;
    TSet<FString> Before;
    TSet<FName> LiveBefore;
    TArray<FString> NewSince(const TSet<FString>& Files) const
    {
        TArray<FString> Out;
        for (const FString& File : ContentFiles())
            if (!Files.Contains(File))
                Out.Add(File);
        return Out;
    }
    bool Ready = false;
    explicit FMade(const FString& Name)
        : List(FPaths::ProjectSavedDir() / TEXT("P4MapKit/Tests") / Name / TEXT("before.txt"))
    {
        if (IFileManager::Get().FileExists(*List))
        {
            TArray<FString> Lines;
            if (!FFileHelper::LoadFileToStringArray(Lines, *List))
                return;
            if (!Remove(NewSince(TSet<FString>(Lines)), P4FixtureRetirement::LivePackages()))
                return;
        }
        LiveBefore = P4FixtureRetirement::LivePackages();
        Before = TSet<FString>(ContentFiles());
        Ready = IFileManager::Get().MakeDirectory(*FPaths::GetPath(List), true) &&
            FFileHelper::SaveStringArrayToFile(
                Before.Array(), *List, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    ~FMade()
    {
        if (Ready && Remove(NewSince(Before), LiveBefore))
            IFileManager::Get().Delete(*List, false, true, true);
    }
};
} //namespace P4KadanImportTests
#endif
