#if P4_MAPKIT_TESTS
#include "P4BlueprintImport.h"
#include "P4OriginalImportSave.h"
#include "P4BlueprintAnimOwnership.h"
#include "P4OriginalClassComparison.h"
#include "P4NativeHash.h"
#include "BlueprintEditorSettings.h"
#include "Animation/AnimBlueprint.h"
#include "AnimationGraph.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "K2Node_AnimGetter.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "HAL/PlatformMisc.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"
#include "UObject/UnrealType.h"
#include <cmath>

namespace P4AnimOwnershipTest
{
static FString FileHash(const FString& File)
{
    FString Digest;
    int64 Bytes = 0;
    return P4HashFile(File, 128ll * 1024 * 1024, Bytes, Digest) ? Digest : FString();
}
struct FState
{
    int32 Graphs = 0, Nodes = 0, Size = 0;
    FString Schema;
    TArray<FString> Inventory;
};
static FState State(UAnimBlueprint* Blueprint, UEdGraphNode* Creator = nullptr, UEdGraphNode* EventCreator = nullptr,
    UEdGraphNode* SequenceCreator = nullptr)
{
    FState Out;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    Out.Graphs = Graphs.Num();
    for (UEdGraph* Graph : Graphs)
    {
        Out.Inventory.Add(
            TEXT("graph ") + Graph->GetPathName(Blueprint) + TEXT(" | ") + Graph->GetClass()->GetPathName());
        for (UEdGraphNode* Node : Graph->Nodes)
            if (Node && Node != Creator && Node != EventCreator && Node != SequenceCreator)
            {
                ++Out.Nodes;
                Out.Inventory.Add(
                    TEXT("node ") + Node->GetPathName(Blueprint) + TEXT(" | ") + Node->GetClass()->GetPathName());
            }
    }
    Out.Size = Blueprint->GeneratedClass->GetPropertiesSize();
    for (TFieldIterator<FProperty> It(Blueprint->GeneratedClass); It; ++It)
        Out.Schema += FString::Printf(TEXT("%s|%s|%d|%d|%llu\n"), *It->GetName(), *It->GetCPPType(), It->ArrayDim,
            It->ElementSize, (unsigned long long)It->GetPropertyFlags());
    return Out;
}
static bool Save(UAnimBlueprint* Blueprint)
{
    // Disposable regression-fixture publication only. Ordinary original-asset editing stays locked.
    P4ImportPublication::FImportScope Import;
    P4ImportPublication::FSaveScope Publication(Blueprint->GetOutermost());
    const FString File = FPackageName::LongPackageNameToFilename(
        Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    Blueprint->MarkPackageDirty();
    return UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, RF_Public | RF_Standalone, *File, GError,
        nullptr, false, true, SAVE_NoError);
}
static TSharedRef<FJsonObject> NodeState(UBlueprint* Blueprint, UEdGraphNode* Node)
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("path"), Node->GetPathName(Blueprint));
    Row->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
    Row->SetBoolField(TEXT("source_owned"), P4AnimOwnership::SourceNode(Node));
    Row->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
    if (auto Call = Cast<UK2Node_CallFunction>(Node))
    {
        auto Function = Call->GetTargetFunction();
        Row->SetStringField(TEXT("function"), Function ? Function->GetPathName() : TEXT("none"));
    }
    TArray<TSharedPtr<FJsonValue>> Pins;
    for (UEdGraphPin* Pin : Node->Pins)
        if (Pin)
        {
            auto Item = MakeShared<FJsonObject>();
            FString Text;
            Pin->ExportTextItem(Text, PPF_Delimited);
            Item->SetStringField(TEXT("native_text"), Text);
            TArray<TSharedPtr<FJsonValue>> Links;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
                if (Linked)
                    Links.Add(MakeShared<FJsonValueString>(
                        Linked->GetOwningNode()->GetPathName(Blueprint) + TEXT(":") + Linked->PinId.ToString()));
            Item->SetArrayField(TEXT("full_links"), Links);
            Pins.Add(MakeShared<FJsonValueObject>(Item));
        }
    Row->SetArrayField(TEXT("ordered_pins"), Pins);
    return Row;
}
} //namespace P4AnimOwnershipTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AnimReimportOwnership, "P4MapKit.Kadan.AnimReimportOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AnimReimportOwnership::RunTest(const FString&)
{
    using namespace P4AnimOwnershipTest;
    const FString Package = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ANIM_OWNERSHIP_PACKAGE"));
    const FString Plans = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ANIM_OWNERSHIP_PLANS"));
    const FString Out = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ANIM_OWNERSHIP_OUT"));
    const FString Mode = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ANIM_OWNERSHIP_MODE"));
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ANIM_OWNERSHIP_HEADER"));
    if (Package.IsEmpty() || Plans.IsEmpty() || Out.IsEmpty() || Header.IsEmpty() ||
        (Mode != TEXT("legacy") && Mode != TEXT("fresh") && Mode != TEXT("unrelated")))
    {
        AddWarning(TEXT(
            "P4MapKit test not run. Set the explicit owned actual AnimBlueprint fixture, plans, source, mode and output."));
        return true;
    }
    const FString Object = Package + TEXT(".") + FPackageName::GetShortName(Package);
    const FString File = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
    if (Mode == TEXT("legacy"))
    {
        const FString Before = FileHash(File);
        if (!TestEqual(TEXT("Legacy source file has a SHA256 receipt"), Before.Len(), 64))
            return false;
        UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, *Object);
        if (!TestNotNull(TEXT("Actual legacy asset loads"), Blueprint))
            return false;
        TestTrue(TEXT("Legacy animation ownership was never recorded"),
            FString(Blueprint->GetOutermost()->GetMetaData()->GetValue(
                        Blueprint, TEXT("P4BlueprintAnimationOwnershipVersion")))
                .IsEmpty());
        // A game Blueprint a 0.2.0 project imported was locked there, so all of its animation is the game's:
        // it is built again and recorded as source. Any other unrecorded animation Blueprint is refused.
        const bool GameOriginal = P4AnimOwnership::LegacyGameOriginal(Blueprint);
        const auto Result = UP4BlueprintImport::RunSelectedNative(
            Plans, Out / TEXT("legacy-result.json"), {Package}, true, {}, true);
        if (GameOriginal)
        {
            TestTrue(TEXT("A 0.2.0 game animation Blueprint reimports"), Result.bOk);
            TestTrue(TEXT("The 0.2.0 game animation Blueprint is completed"),
                Result.CompletedPackages.Contains(FName(*Package)));
            UAnimBlueprint* After = LoadObject<UAnimBlueprint>(nullptr, *Object);
            TestTrue(TEXT("Its animation ownership is recorded"),
                After &&
                    FString(After->GetOutermost()->GetMetaData()->GetValue(
                        After, TEXT("P4BlueprintAnimationOwnershipVersion"))) == TEXT("1"));
            return !HasAnyErrors();
        }
        TestFalse(TEXT("Ambiguous legacy animation is not upgraded"), Result.bOk);
        TestTrue(TEXT("Legacy refusal states the missing animation ownership"),
            FString::Join(Result.Errors, TEXT("\n")).Contains(TEXT("legacy-animation-ownership-unrecorded")));
        TestTrue(TEXT("No legacy package was completed"), Result.CompletedPackages.Num() == 0);
        TestEqual(TEXT("Saved legacy asset remains byte-identical"), FileHash(File), Before);
        return !HasAnyErrors();
    }
    if (!TestFalse(TEXT("Fresh control has no prior package on disk"), FPackageName::DoesPackageExist(Package)))
        return false;
    if (Mode == TEXT("unrelated"))
    {
        const FString Broken = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ANIM_OWNERSHIP_BROKEN"));
        if (!TestTrue(TEXT("The unrelated refusal uses a scoped disposable second plan"),
                Broken.StartsWith(TEXT("/Game/KadanAnimOwnershipControl/"))))
            return false;
        const auto Result = UP4BlueprintImport::RunSelectedNative(
            Plans, Out / TEXT("unrelated-result.json"), {Package, Broken}, false, {}, true);
        TestFalse(TEXT("An unrelated missing-parent declaration refuses the overall run"), Result.bOk);
        TestTrue(TEXT("The actual missing-parent row caused the refusal"),
            FString::Join(Result.Errors, TEXT("\n")).Contains(Broken));
        TestFalse(TEXT("The unsealed source animation is never reported complete"),
            Result.CompletedPackages.Contains(FName(*Package)));
        auto Pending = LoadObject<UAnimBlueprint>(nullptr, *Object);
        if (!TestNotNull(TEXT("The independent source animation did build and save before the refusal"), Pending))
            return false;
        TestTrue(TEXT("Unsealed saved animation retains reconstruction-incomplete protection"),
            P4BlueprintReconstructionIncomplete(Pending));
        TestTrue(TEXT("Unsealed saved animation has no content baseline"),
            FString(
                Pending->GetOutermost()->GetMetaData()->GetValue(Pending, TEXT("P4BlueprintAnimationSourceContent")))
                .IsEmpty());
        TestTrue(TEXT("Pending animation exists on disk for the real save-path control"),
            FPackageName::DoesPackageExist(Package));
        TestFalse(
            TEXT("The missing native parent never creates an unrelated asset"), FPackageName::DoesPackageExist(Broken));
        return !HasAnyErrors();
    }
    auto Settings = GetMutableDefault<UBlueprintEditorSettings>();
    const auto PreviousSavePolicy = Settings->SaveOnCompile;
    Settings->SaveOnCompile = SoC_Always;
    ON_SCOPE_EXIT
    {
        Settings->SaveOnCompile = PreviousSavePolicy;
    };
    FText Canonical;
    TestTrue(TEXT("Plain generated editor base labels have a canonical form"),
        P4AnimOwnership::CanonicalEditorLabel(FText::FromString(TEXT("Look At")), Canonical));
    TestTrue(TEXT("Engine-shaped empty-namespace display keys have a canonical form"),
        P4AnimOwnership::CanonicalEditorLabel(
            FText::ChangeKey(
                TEXT(""), FGuid::NewGuid().ToString(EGuidFormats::Digits), FText::FromString(TEXT("Look At"))),
            Canonical));
    TestFalse(TEXT("Authored invariant text retains its distinct identity"),
        P4AnimOwnership::CanonicalEditorLabel(FText::AsCultureInvariant(TEXT("Look At")), Canonical));
    TestFalse(TEXT("Same-visible-text format history remains strict"),
        P4AnimOwnership::CanonicalEditorLabel(
            FText::Format(FText::FromString(TEXT("{0}")), FText::FromString(TEXT("Look At"))), Canonical));
    TestFalse(TEXT("Numeric text history remains strict"),
        P4AnimOwnership::CanonicalEditorLabel(FText::AsNumber(1), Canonical));
    TestFalse(TEXT("Transformed text history remains strict"),
        P4AnimOwnership::CanonicalEditorLabel(FText::FromString(TEXT("look at")).ToUpper(), Canonical));
    TestFalse(TEXT("Authored namespace identity remains strict"),
        P4AnimOwnership::CanonicalEditorLabel(
            FText::ChangeKey(
                TEXT("Creator"), FGuid::NewGuid().ToString(EGuidFormats::Digits), FText::FromString(TEXT("Look At"))),
            Canonical));
    TestFalse(TEXT("Authored non-GUID key identity remains strict"),
        P4AnimOwnership::CanonicalEditorLabel(
            FText::ChangeKey(TEXT(""), TEXT("CreatorLabel"), FText::FromString(TEXT("Look At"))), Canonical));
    // Observe only the ordinary import's two cold passes. No fault hooks or mutation.
    TArray<FString> PreviousCold;
    TMap<FString, TSharedRef<FJsonObject>> PreviousNodes;
    bool OwnedCold = false;
    int32 ColdPass = 0;
    const FDelegateHandle ColdHandle = FCoreUObjectDelegates::OnPackageReloaded.AddLambda(
        [&](EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
        {
            if (Phase == EPackageReloadPhase::PrePackageFixup && Event && Event->GetNewPackage() &&
                Event->GetNewPackage()->GetName() == Package)
                OwnedCold = true;
            if (Phase != EPackageReloadPhase::PostBatchPostGC || !OwnedCold)
                return;
            OwnedCold = false;
            auto CurrentBlueprint = FindObject<UAnimBlueprint>(nullptr, *Object);
            if (!CurrentBlueprint)
                return;
            TArray<FString> Current;
            FString Digest;
            P4AnimOwnership::Fingerprint(CurrentBlueprint, Digest, &Current);
            TMap<FString, TSharedRef<FJsonObject>> Nodes;
            TArray<UEdGraph*> Graphs;
            CurrentBlueprint->GetAllGraphs(Graphs);
            for (UEdGraph* Graph : Graphs)
                for (UEdGraphNode* Node : Graph->Nodes)
                    if (Node)
                        Nodes.Add(Node->GetPathName(CurrentBlueprint), NodeState(CurrentBlueprint, Node));
            auto Row = MakeShared<FJsonObject>();
            Row->SetNumberField(TEXT("cold_pass"), ++ColdPass);
            Row->SetStringField(TEXT("current"), Digest);
            TArray<TSharedPtr<FJsonValue>> Changes;
            FString Owner;
            for (int32 Index = 0; Index < FMath::Max(Current.Num(), PreviousCold.Num()); ++Index)
            {
                if (Index > 0 && Current.IsValidIndex(Index) && Current[Index].StartsWith(TEXT("/Script/")) &&
                    !Current[Index].Contains(TEXT(":")) && Current[Index - 1].StartsWith(TEXT("AnimGraph")))
                    Owner = Current[Index - 1];
                if (PreviousCold.Num() && Changes.Num() < 8 &&
                    (Current.IsValidIndex(Index) != PreviousCold.IsValidIndex(Index) ||
                        (Current.IsValidIndex(Index) && Current[Index] != PreviousCold[Index])))
                {
                    auto Delta = MakeShared<FJsonObject>();
                    Delta->SetNumberField(TEXT("index"), Index);
                    Delta->SetStringField(TEXT("owner"), Owner);
                    Delta->SetStringField(TEXT("previous"),
                        PreviousCold.IsValidIndex(Index) ? PreviousCold[Index].Left(2000) : TEXT("<absent>"));
                    Delta->SetStringField(
                        TEXT("current"), Current.IsValidIndex(Index) ? Current[Index].Left(2000) : TEXT("<absent>"));
                    if (const auto Prior = PreviousNodes.Find(Owner))
                        Delta->SetObjectField(TEXT("previous_node"), *Prior);
                    if (const auto Now = Nodes.Find(Owner))
                        Delta->SetObjectField(TEXT("current_node"), *Now);
                    TArray<TSharedPtr<FJsonValue>> PriorHelpers, CurrentHelpers;
                    const FString PreviousText = PreviousCold.IsValidIndex(Index) ? PreviousCold[Index] : FString();
                    const FString CurrentText = Current.IsValidIndex(Index) ? Current[Index] : FString();
                    const FString GraphPath = Owner.Contains(TEXT("."))
                        ? Owner.Left(Owner.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd))
                        : FString();
                    for (const auto& Item : PreviousNodes)
                        if (Item.Key.StartsWith(GraphPath + TEXT(".")) &&
                            PreviousText.Contains(FPackageName::ObjectPathToObjectName(Item.Key)) &&
                            Item.Value->HasField(TEXT("function")))
                            PriorHelpers.Add(MakeShared<FJsonValueObject>(Item.Value));
                    for (const auto& Item : Nodes)
                        if (Item.Key.StartsWith(GraphPath + TEXT(".")) &&
                            CurrentText.Contains(FPackageName::ObjectPathToObjectName(Item.Key)) &&
                            Item.Value->HasField(TEXT("function")))
                            CurrentHelpers.Add(MakeShared<FJsonValueObject>(Item.Value));
                    Delta->SetArrayField(TEXT("previous_helpers"), PriorHelpers);
                    Delta->SetArrayField(TEXT("current_helpers"), CurrentHelpers);
                    Changes.Add(MakeShared<FJsonValueObject>(Delta));
                }
            }
            Row->SetArrayField(TEXT("first_changes"), Changes);
            FString Text;
            FJsonSerializer::Serialize(Row, TJsonWriterFactory<>::Create(&Text));
            FFileHelper::SaveStringToFile(Text, *(Out / FString::Printf(TEXT("cold-pass-%d.json"), ColdPass)),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            PreviousCold = MoveTemp(Current);
            PreviousNodes = MoveTemp(Nodes);
        });
    ON_SCOPE_EXIT
    {
        FCoreUObjectDelegates::OnPackageReloaded.Remove(ColdHandle);
    };
    const auto Initial = UP4BlueprintImport::RunSelectedNative(
        Plans, Out / TEXT("fresh-result.json"), {Package}, false, {}, true);
    if (!TestTrue(
            TEXT("Actual source fresh native import completes"), Initial.bOk && Initial.CompletedPackages.Num() == 1))
    {
        for (const auto& Error : Initial.Errors)
            AddError(Error);
        return false;
    }
    UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, *Object);
    if (!TestNotNull(TEXT("Actual fresh animation Blueprint exists"), Blueprint))
        return false;
    TestTrue(TEXT("Initial source seal restores caller SaveOnCompile=Always"), Settings->SaveOnCompile == SoC_Always);
    TArray<FString> PriorWitness;
    const auto TraceContent = [&](const TCHAR* Stage)
    {
        FString Digest, RawDigest;
        TArray<FString> Current;
        P4AnimOwnership::Fingerprint(Blueprint, Digest, &Current);
        P4AnimOwnership::Fingerprint(Blueprint, RawDigest, nullptr, false);
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("stage"), Stage);
        Row->SetStringField(TEXT("current"), Digest);
        Row->SetStringField(TEXT("raw_editor_label_current"), RawDigest);
        Row->SetStringField(TEXT("baseline"),
            Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint, TEXT("P4BlueprintAnimationSourceContent")));
        TArray<TSharedPtr<FJsonValue>> Changed;
        for (int32 Index = 0; Index < FMath::Max(Current.Num(), PriorWitness.Num()) && Changed.Num() < 8; ++Index)
            if (Current.IsValidIndex(Index) != PriorWitness.IsValidIndex(Index) ||
                (Current.IsValidIndex(Index) && Current[Index] != PriorWitness[Index]))
            {
                auto Delta = MakeShared<FJsonObject>();
                Delta->SetNumberField(TEXT("index"), Index);
                Delta->SetStringField(TEXT("previous"),
                    PriorWitness.IsValidIndex(Index) ? PriorWitness[Index].Left(2000) : TEXT("<absent>"));
                Delta->SetStringField(
                    TEXT("current"), Current.IsValidIndex(Index) ? Current[Index].Left(2000) : TEXT("<absent>"));
                if (Current.IsValidIndex(Index - 1))
                    Delta->SetStringField(TEXT("prior_field"), Current[Index - 1].Left(1000));
                Changed.Add(MakeShared<FJsonValueObject>(Delta));
            }
        Row->SetArrayField(TEXT("first_changes_from_prior_stage"), Changed);
        FString Json;
        FJsonSerializer::Serialize(Row, TJsonWriterFactory<>::Create(&Json));
        FFileHelper::SaveStringToFile(
            Json, *(Out / (FString(Stage) + TEXT(".json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        PriorWitness = MoveTemp(Current);
    };
    TraceContent(TEXT("content-fresh-saved"));
    const auto ColdReload = [&]()
    {
        P4ImportPublication::FImportScope Import;
        FText Error;
        if (!TestTrue(TEXT("Actual saved animation package cold-reloads"),
                UPackageTools::ReloadPackages(
                    {Blueprint->GetOutermost()}, Error, EReloadPackagesInteractionMode::AssumePositive)))
        {
            AddInfo(Error.ToString());
            return false;
        }
        Blueprint = LoadObject<UAnimBlueprint>(nullptr, *Object);
        return TestNotNull(TEXT("Freshly reloaded animation Blueprint resolves"), Blueprint);
    };
    if (!ColdReload())
        return false;
    TraceContent(TEXT("content-cold-loaded"));
    const auto Baseline = State(Blueprint);
    TestTrue(TEXT("Fresh class stays below the engine GC offset boundary"), Baseline.Size < 0x80000);
    auto CheckSource = [&](int32 Cycle)
    {
        const auto Compared = P4CompareOriginalClassNative(Header, FPaths::ChangeExtension(Header, TEXT("uexp")));
        const FString Json = P4OriginalClassComparisonJson(Compared);
        FFileHelper::SaveStringToFile(Json, *(Out / FString::Printf(TEXT("source-class-%d.json"), Cycle)),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        TestTrue(TEXT("Actual cooked source class comparison remains valid"), Compared.IsValid());
        TestTrue(TEXT("Actual source held defaults/schema remain equal"), Compared.Same && Compared.Differences == 0);
        TestEqual(TEXT("The exact source has no values this engine cannot hold"), Compared.ValuesNotHeld, 0);
        TestTrue(TEXT("The exact pinned source compares all 2270 values"),
            Compared.ComparedKnown && Compared.Compared == 2270);
        TestTrue(TEXT("Complete readable source/native facts match exactly"),
            Compared.DefaultsReadable && Compared.GameDefaultFactsDigest.Len() == 64 &&
                Compared.DefaultFactsDigest == Compared.GameDefaultFactsDigest);
        if (!Compared.IsValid() || !Compared.Same)
            AddInfo(Compared.ErrorDetail);
    };
    CheckSource(0);
    TraceContent(TEXT("content-source-compared"));
    for (int32 Cycle = 1; Cycle <= 2; ++Cycle)
    {
        const auto Result = UP4BlueprintImport::RunSelectedNative(
            Plans, Out / FString::Printf(TEXT("source-repeat-%d.json"), Cycle), {Package}, true, {}, true);
        if (!TestTrue(
                TEXT("Untouched source repeated import completes"), Result.bOk && Result.CompletedPackages.Num() == 1))
        {
            for (const auto& Error : Result.Errors)
                AddError(Error);
            return false;
        }
        Blueprint = LoadObject<UAnimBlueprint>(nullptr, *Object);
        if (!ColdReload())
            return false;
        const auto Current = State(Blueprint);
        TestEqual(TEXT("Untouched source graph count remains exact"), Current.Graphs, Baseline.Graphs);
        TestEqual(TEXT("Untouched source node count remains exact"), Current.Nodes, Baseline.Nodes);
        TestEqual(TEXT("Untouched source generated size remains exact"), Current.Size, Baseline.Size);
        TestEqual(TEXT("Untouched source generated schema remains exact"), Current.Schema, Baseline.Schema);
        CheckSource(Cycle);
    }
    const FString SourceSeal = Blueprint->GetOutermost()->GetMetaData()->GetValue(
        Blueprint, TEXT("P4BlueprintAnimationSourceContent"));
    FString ActualPlanText;
    TSharedPtr<FJsonObject> ActualPlan;
    const FString ActualPlanFile = Plans / TEXT("import") / (Package.Mid(1) + TEXT(".json"));
    if (!TestTrue(TEXT("Admission uses the actual retained animation plan"),
            FFileHelper::LoadFileToString(ActualPlanText, *ActualPlanFile) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ActualPlanText), ActualPlan) &&
                ActualPlan.IsValid() && ActualPlan->GetStringField(TEXT("package")) == Package &&
                ActualPlan->HasTypedField<EJson::Object>(TEXT("anim"))))
        return false;
    const auto SavedCreatorAdmission = [&](const TCHAR* Stage)
    {
        TraceContent(Stage);
        FString Current, Why;
        const bool SourceUnchanged = SourceSeal.Len() == 40 &&
            FString(Blueprint->GetOutermost()->GetMetaData()->GetValue(
                Blueprint, TEXT("P4BlueprintAnimationSourceContent"))) == SourceSeal &&
            P4AnimOwnership::Fingerprint(Blueprint, Current) && Current == SourceSeal;
        const bool Admitted = P4AnimOwnership::Preflight(Blueprint, ActualPlan, Why);
        TestTrue(TEXT("Ordinary saved independent creators retain the original source fingerprint without recapture"),
            SourceUnchanged);
        TestTrue(
            TEXT("The actual ownership preflight admits independent creators before cold reconstruction"), Admitted);
        if (!Admitted)
            AddInfo(Why);
        return SourceUnchanged && Admitted;
    };
    UAnimationGraph* Top = nullptr;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        if (auto Animation = Cast<UAnimationGraph>(Graph))
            Top = Animation;
    if (!TestNotNull(TEXT("Fresh source root graph exists"), Top))
        return false;
    auto Creator = NewObject<UK2Node_CallFunction>(Top, TEXT("KadanCreatorIndependent"), RF_Transactional);
    UFunction* PureFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Abs"));
    if (!TestNotNull(TEXT("A genuine independent native pure math function exists"), PureFunction))
        return false;
    Creator->SetFromFunction(PureFunction);
    Creator->CreateNewGuid();
    Creator->AllocateDefaultPins();
    Top->AddNode(Creator, false, false);
    Creator->NodePosX = 777;
    Creator->NodePosY = 888;
    Creator->NodeComment = TEXT("Independent creator control");
    auto CreatorValue = Creator->FindPin(TEXT("A"));
    if (!TestNotNull(TEXT("Independent native function has its actual numeric input"), CreatorValue))
        return false;
    CreatorValue->DefaultValue = TEXT("3.25");
    const FGuid CreatorGuid = Creator->NodeGuid;
    const FName CreatorName = Creator->GetFName();
    auto Events = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (!TestNotNull(TEXT("Actual source event graph exists"), Events))
        return false;
    TestNotNull(TEXT("Actual source custom event has its generated function"),
        Blueprint->GeneratedClass->FindFunctionByName(TEXT("AnimNotify_Nukare")));
    auto EventCreator = NewObject<UEdGraphNode_Comment>(Events, TEXT("KadanCreatorEventComment"), RF_Transactional);
    EventCreator->CreateNewGuid();
    Events->AddNode(EventCreator, false, false);
    EventCreator->NodeComment = TEXT("Creator event graph annotation");
    EventCreator->NodePosX = 901;
    EventCreator->NodePosY = 902;
    EventCreator->NodeWidth = 333;
    EventCreator->NodeHeight = 222;
    const FGuid EventCreatorGuid = EventCreator->NodeGuid;
    const FName EventCreatorName = EventCreator->GetFName();
    UAnimGraphNode_SequencePlayer* SourceSequence = nullptr;
    TArray<UEdGraph*> InitialGraphs;
    Blueprint->GetAllGraphs(InitialGraphs);
    for (UEdGraph* Graph : InitialGraphs)
        for (UEdGraphNode* Node : Graph->Nodes)
            if (auto Sequence = Cast<UAnimGraphNode_SequencePlayer>(Node))
                if (P4AnimOwnership::SourceNode(Sequence))
                    SourceSequence = Sequence;
    if (!TestTrue(TEXT("A real source sequence supplies the independent creator asset"),
            SourceSequence && SourceSequence->Node.Sequence))
        return false;
    auto SequenceCreator = NewObject<UAnimGraphNode_SequencePlayer>(
        Top, TEXT("KadanCreatorSequence"), RF_Transactional);
    auto CreatorAsset = SourceSequence->Node.Sequence;
    SequenceCreator->Node.Sequence = CreatorAsset;
    SequenceCreator->Node.PlayRate = 1.5f;
    SequenceCreator->CreateNewGuid();
    SequenceCreator->PostPlacedNewNode();
    SequenceCreator->AllocateDefaultPins();
    Top->AddNode(SequenceCreator, false, false);
    SequenceCreator->NodePosX = 999;
    SequenceCreator->NodePosY = 1111;
    SequenceCreator->NodeComment = TEXT("Independent creator sequence");
    const FGuid SequenceCreatorGuid = SequenceCreator->NodeGuid;
    const FName SequenceCreatorName = SequenceCreator->GetFName();
    TArray<FGuid> SequenceCreatorPins;
    for (UEdGraphPin* Pin : SequenceCreator->Pins)
        if (Pin)
            SequenceCreatorPins.Add(Pin->PinId);
    if (!TestTrue(TEXT("A genuine independent creator math node is saved"), Save(Blueprint)))
        return false;
    if (!SavedCreatorAdmission(TEXT("content-creator-saved")))
        return false;
    // An actual creator animation can change the compiler's allocation order even when
    // isolated: pruning uses RemoveAtSwap. Establish the edited native state by ordinary
    // compilation before importing again; never re-seal source metadata for this control.
    {
        TGuardValue<decltype(Settings->SaveOnCompile)> NoAutosave(Settings->SaveOnCompile, SoC_Never);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(
            Blueprint, EBlueprintCompileOptions::SkipGarbageCollection | EBlueprintCompileOptions::SkipSave);
    }
    if (!TestTrue(TEXT("The intentional creator graph compiles normally"), Blueprint->Status != BS_Error))
        return false;
    if (!TestTrue(TEXT("The normally compiled creator graph saves"), Save(Blueprint)))
        return false;
    if (!SavedCreatorAdmission(TEXT("content-creator-compiled-saved")) || !ColdReload())
        return false;
    Top = nullptr;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        if (auto Animation = Cast<UAnimationGraph>(Graph))
            Top = Animation;
    Events = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    Creator = Top ? FindObject<UK2Node_CallFunction>(Top, *CreatorName.ToString()) : nullptr;
    EventCreator = Events ? FindObject<UEdGraphNode_Comment>(Events, *EventCreatorName.ToString()) : nullptr;
    SequenceCreator = Top ? FindObject<UAnimGraphNode_SequencePlayer>(Top, *SequenceCreatorName.ToString()) : nullptr;
    if (!TestTrue(TEXT("All independent creator nodes resolve after normal edited compilation"),
            Creator && EventCreator && SequenceCreator))
        return false;
    FString EditedSourceDigest;
    if (!TestTrue(TEXT("Normal creator compilation retains the original source-content seal without recapture"),
            SourceSeal.Len() == 40 &&
                FString(Blueprint->GetOutermost()->GetMetaData()->GetValue(
                    Blueprint, TEXT("P4BlueprintAnimationSourceContent"))) == SourceSeal &&
                P4AnimOwnership::Fingerprint(Blueprint, EditedSourceDigest) && EditedSourceDigest == SourceSeal))
        return false;
    const auto CreatorBaseline = State(Blueprint, Creator, EventCreator, SequenceCreator);
    const auto CreatorNative = P4CompareOriginalClassNative(Header, FPaths::ChangeExtension(Header, TEXT("uexp")));
    if (!TestTrue(TEXT("Normally compiled edited native facts are fully readable"),
            CreatorNative.IsValid() && CreatorNative.DefaultsReadable && CreatorNative.ValuesNotHeld == 0 &&
                CreatorNative.ComparedKnown && CreatorNative.Compared == 2270 &&
                CreatorNative.GameDefaultFactsDigest.Len() == 64 && CreatorNative.DefaultFactsDigest.Len() == 64))
        return false;
    FFileHelper::SaveStringToFile(P4OriginalClassComparisonJson(CreatorNative),
        *(Out / TEXT("creator-class-before.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    for (int32 Cycle = 1; Cycle <= 2; ++Cycle)
    {
        const auto Result = UP4BlueprintImport::RunSelectedNative(
            Plans, Out / FString::Printf(TEXT("creator-repeat-%d.json"), Cycle), {Package}, true, {}, true);
        if (!TestTrue(
                TEXT("Actual repeated source import completes"), Result.bOk && Result.CompletedPackages.Num() == 1))
        {
            for (const auto& Error : Result.Errors)
                AddError(Error);
            return false;
        }
        Blueprint = LoadObject<UAnimBlueprint>(nullptr, *Object);
        if (!ColdReload())
            return false;
        Top = nullptr;
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
            if (auto Animation = Cast<UAnimationGraph>(Graph))
                Top = Animation;
        Creator = Top ? FindObject<UK2Node_CallFunction>(Top, *CreatorName.ToString()) : nullptr;
        if (!TestNotNull(TEXT("Independent creator math node survives disk reload"), Creator))
            return false;
        Events = FBlueprintEditorUtils::FindEventGraph(Blueprint);
        EventCreator = Events ? FindObject<UEdGraphNode_Comment>(Events, *EventCreatorName.ToString()) : nullptr;
        if (!TestNotNull(TEXT("Independent event graph comment survives disk reload"), EventCreator))
            return false;
        SequenceCreator = Top ? FindObject<UAnimGraphNode_SequencePlayer>(Top, *SequenceCreatorName.ToString())
                              : nullptr;
        if (!TestNotNull(TEXT("Independent creator animation node survives disk reload"), SequenceCreator))
            return false;
        const auto Current = State(Blueprint, Creator, EventCreator, SequenceCreator);
        TestEqual(TEXT("Source graph count is idempotent"), Current.Graphs, Baseline.Graphs);
        TestEqual(TEXT("Source node count is idempotent"), Current.Nodes, Baseline.Nodes);
        TestEqual(TEXT("Edited generated class size is idempotent"), Current.Size, CreatorBaseline.Size);
        TestEqual(
            TEXT("Complete edited generated property schema is idempotent"), Current.Schema, CreatorBaseline.Schema);
        if (Current.Graphs != Baseline.Graphs || Current.Nodes != Baseline.Nodes)
        {
            int32 Shown = 0;
            for (const auto& Item : Baseline.Inventory)
                if (!Current.Inventory.Contains(Item) && Shown++ < 8)
                    AddInfo(TEXT("Missing after repeat: ") + Item);
            Shown = 0;
            for (const auto& Item : Current.Inventory)
                if (!Baseline.Inventory.Contains(Item) && Shown++ < 8)
                    AddInfo(TEXT("Added after repeat: ") + Item);
        }
        TestTrue(TEXT("Creator node object, name, GUID and graph membership survive"),
            IsValid(Creator) && Creator->GetFName() == CreatorName && Creator->NodeGuid == CreatorGuid &&
                Top->Nodes.Contains(Creator));
        TestTrue(TEXT("Creator function binding, layout, comment and actual pin default survive"),
            Creator->GetTargetFunction() == PureFunction && Creator->NodePosX == 777 && Creator->NodePosY == 888 &&
                Creator->NodeComment == TEXT("Independent creator control") && Creator->FindPin(TEXT("A")) &&
                Creator->FindPin(TEXT("A"))->DefaultValue == TEXT("3.25"));
        TestTrue(TEXT("Creator event annotation identity, content, layout and membership survive"),
            EventCreator->NodeGuid == EventCreatorGuid &&
                EventCreator->NodeComment == TEXT("Creator event graph annotation") && EventCreator->NodePosX == 901 &&
                EventCreator->NodePosY == 902 && EventCreator->NodeWidth == 333 && EventCreator->NodeHeight == 222 &&
                Events->Nodes.Contains(EventCreator));
        TArray<FGuid> CurrentCreatorPins;
        for (UEdGraphPin* Pin : SequenceCreator->Pins)
            if (Pin)
                CurrentCreatorPins.Add(Pin->PinId);
        TestTrue(TEXT("Creator animation identity, asset, value, layout, pins and membership survive"),
            SequenceCreator->NodeGuid == SequenceCreatorGuid && SequenceCreator->Node.Sequence == CreatorAsset &&
                SequenceCreator->Node.PlayRate == 1.5f && SequenceCreator->NodePosX == 999 &&
                SequenceCreator->NodePosY == 1111 &&
                SequenceCreator->NodeComment == TEXT("Independent creator sequence") &&
                CurrentCreatorPins == SequenceCreatorPins && Top->Nodes.Contains(SequenceCreator));
        TestNotNull(TEXT("Actual source custom event function survives reimport"),
            Blueprint->GeneratedClass->FindFunctionByName(TEXT("AnimNotify_Nukare")));
        const auto Compared = P4CompareOriginalClassNative(Header, FPaths::ChangeExtension(Header, TEXT("uexp")));
        FFileHelper::SaveStringToFile(P4OriginalClassComparisonJson(Compared),
            *(Out / FString::Printf(TEXT("creator-class-%d.json"), Cycle)),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        TestTrue(TEXT("Edited native facts remain fully readable"), Compared.IsValid() && Compared.ValuesNotHeld == 0);
        TestEqual(TEXT("The actual source facts remain unchanged"), Compared.GameDefaultFactsDigest,
            CreatorNative.GameDefaultFactsDigest);
        TestEqual(TEXT("Reimport preserves the already-compiled creator native facts exactly"),
            Compared.DefaultFactsDigest, CreatorNative.DefaultFactsDigest);
        TestEqual(TEXT("Reimport preserves the exact edited-state comparison count"), Compared.Compared,
            CreatorNative.Compared);
        TestTrue(
            TEXT("Repeated source seal restores caller SaveOnCompile=Always"), Settings->SaveOnCompile == SoC_Always);
    }
    UK2Node_CustomEvent* SourceEvent = nullptr;
    for (UEdGraphNode* Node : Events->Nodes)
        if (auto Event = Cast<UK2Node_CustomEvent>(Node))
            if (Event->CustomFunctionName == TEXT("AnimNotify_Nukare"))
                SourceEvent = Event;
    if (!TestNotNull(TEXT("Actual source event node exists for the creator-link control"), SourceEvent))
        return false;
    auto EventDependent = NewObject<UK2Node_CallFunction>(Events, TEXT("KadanCreatorEventDependent"), RF_Transactional);
    auto Print = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString"));
    if (!TestNotNull(TEXT("A genuine creator impure call exists"), Print))
        return false;
    EventDependent->SetFromFunction(Print);
    EventDependent->CreateNewGuid();
    EventDependent->AllocateDefaultPins();
    Events->AddNode(EventDependent, false, false);
    auto EventOutput = SourceEvent->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
    auto CreatorInput = EventDependent->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
    if (!TestTrue(TEXT("A real creator execution wire is attached to the source event"),
            EventOutput && CreatorInput && Events->GetSchema()->TryCreateConnection(EventOutput, CreatorInput)))
        return false;
    if (!TestTrue(TEXT("The creator event-link control is saved"), Save(Blueprint)))
        return false;
    const FString BeforeEventRefusal = FileHash(File);
    const auto EventRefused = UP4BlueprintImport::RunSelectedNative(
        Plans, Out / TEXT("creator-event-link-refusal.json"), {Package}, true, {}, true);
    TestFalse(TEXT("A creator reference to a replaced source event is refused"), EventRefused.bOk);
    TestTrue(TEXT("Refusal identifies the source dependency before event removal"),
        FString::Join(EventRefused.Errors, TEXT("\n")).Contains(TEXT("animation-creator-node-depends-on-source")));
    TestTrue(TEXT("No event-dependent creator package is completed"), EventRefused.CompletedPackages.Num() == 0);
    TestEqual(TEXT("Saved creator event-link refusal preserves the complete file"), FileHash(File), BeforeEventRefusal);
    TestTrue(TEXT("The source event, creator and both real wire ends remain"),
        Events->Nodes.Contains(SourceEvent) && Events->Nodes.Contains(EventDependent) &&
            EventOutput->LinkedTo.Contains(CreatorInput) && CreatorInput->LinkedTo.Contains(EventOutput));
    Events->RemoveNode(EventDependent);
    if (!TestTrue(TEXT("Only the owned creator event-link control is removed"), Save(Blueprint)))
        return false;
    auto Getter = NewObject<UK2Node_VariableGet>(Top, TEXT("KadanCreatorSourceVariableGetter"), RF_Transactional);
    Getter->VariableReference.SetSelfMember(TEXT("Avatar"));
    Getter->CreateNewGuid();
    Getter->AllocateDefaultPins();
    Top->AddNode(Getter, false, false);
    if (!TestTrue(TEXT("Creator source-variable getter is saved"), Save(Blueprint)))
        return false;
    const FString BeforeGetterRefusal = FileHash(File);
    const auto GetterRefused = UP4BlueprintImport::RunSelectedNative(
        Plans, Out / TEXT("creator-getter-refusal.json"), {Package}, true, {}, true);
    TestFalse(TEXT("A creator getter of a rebuilt source variable is refused"), GetterRefused.bOk);
    TestTrue(TEXT("Refusal identifies source-variable dependency before variable removal"),
        FString::Join(GetterRefused.Errors, TEXT("\n"))
            .Contains(TEXT("animation-creator-node-depends-on-source-variable")));
    TestTrue(
        TEXT("No source-variable-dependent creator package was completed"), GetterRefused.CompletedPackages.Num() == 0);
    TestEqual(TEXT("Saved creator getter refusal preserves the whole asset file"), FileHash(File), BeforeGetterRefusal);
    TestTrue(TEXT("Creator getter remains attached with its actual member binding"),
        Top->Nodes.Contains(Getter) && Getter->VariableReference.GetMemberName() == TEXT("Avatar"));
    Top->RemoveNode(Getter);
    if (!TestTrue(TEXT("Only the owned creator-getter control is removed"), Save(Blueprint)))
        return false;
    UK2Node_MathExpression* SourceMath = nullptr;
    UEdGraphPin* GeneratedDefault = nullptr;
    TArray<UEdGraph*> MathGraphs;
    Blueprint->GetAllGraphs(MathGraphs);
    for (UEdGraph* Graph : MathGraphs)
        if (P4AnimOwnership::DerivedMathGraph(Graph))
            for (UEdGraphNode* Node : Graph->Nodes)
                if (auto Call = Cast<UK2Node_CallFunction>(Node))
                {
                    auto Function = Call->GetTargetFunction();
                    auto B = Call->FindPin(TEXT("B"));
                    if (Function && Function->GetFName() == TEXT("Divide_FloatFloat") && B &&
                        B->DefaultValue.Contains(TEXT("100")))
                    {
                        SourceMath = CastChecked<UK2Node_MathExpression>(Graph->GetOuter());
                        GeneratedDefault = B;
                    }
                }
    if (!TestNotNull(TEXT("Actual source MathExpression has its exact regenerated divide helper"), GeneratedDefault))
        return false;
    const FString OriginalDefault = GeneratedDefault->DefaultValue;
    GeneratedDefault->DefaultValue = TEXT("101.000000");
    if (!TestTrue(TEXT("A saved generated-helper default edit has a real file"), Save(Blueprint)))
        return false;
    const FString BeforeMathRefusal = FileHash(File);
    const auto MathRefused = UP4BlueprintImport::RunSelectedNative(
        Plans, Out / TEXT("math-helper-default-refusal.json"), {Package}, true, {}, true);
    TestFalse(TEXT("MathExpression identity aliases never hide helper default edits"), MathRefused.bOk);
    TestTrue(TEXT("Complete generated helper semantics remain guarded"),
        FString::Join(MathRefused.Errors, TEXT("\n")).Contains(TEXT("animation-source-content-changed")));
    TestEqual(
        TEXT("Saved generated-helper edit refusal preserves the complete file"), FileHash(File), BeforeMathRefusal);
    GeneratedDefault->DefaultValue = OriginalDefault;
    if (!TestTrue(TEXT("Only the owned generated-helper edit is restored"), Save(Blueprint)))
        return false;
    auto Input = SourceMath->GetEntryNode()->Pins.Num() ? SourceMath->GetEntryNode()->Pins[0] : nullptr;
    if (!TestNotNull(TEXT("MathExpression retains an authored input pin"), Input))
        return false;
    const FGuid OriginalInputId = Input->PinId;
    Input->PinId = FGuid::NewGuid();
    if (!TestTrue(TEXT("Authored MathExpression input-pin identity edit is saved"), Save(Blueprint)))
        return false;
    const FString BeforeInputRefusal = FileHash(File);
    const auto InputRefused = UP4BlueprintImport::RunSelectedNative(
        Plans, Out / TEXT("math-input-identity-refusal.json"), {Package}, true, {}, true);
    TestFalse(TEXT("Authored MathExpression input identity is never aliased"), InputRefused.bOk);
    TestTrue(TEXT("Authored input identity remains in the full content guard"),
        FString::Join(InputRefused.Errors, TEXT("\n")).Contains(TEXT("animation-source-content-changed")));
    TestEqual(
        TEXT("Saved authored-input identity refusal preserves the complete file"), FileHash(File), BeforeInputRefusal);
    Input->PinId = OriginalInputId;
    if (!TestTrue(TEXT("Only the owned input identity control is restored"), Save(Blueprint)))
        return false;
    const bool OriginalWrapper = Input->PinType.bIsUObjectWrapper;
    Input->PinType.bIsUObjectWrapper = !OriginalWrapper;
    if (!TestTrue(TEXT("A non-self authored wrapper-flag edit is saved"), Save(Blueprint)))
        return false;
    const FString BeforeWrapperRefusal = FileHash(File);
    const auto WrapperRefused = UP4BlueprintImport::RunSelectedNative(
        Plans, Out / TEXT("non-self-wrapper-refusal.json"), {Package}, true, {}, true);
    TestFalse(TEXT("Only the exact constructor-undefined native self flag is normalized"), WrapperRefused.bOk);
    TestTrue(TEXT("A non-self wrapper-flag edit remains in the strict guard"),
        FString::Join(WrapperRefused.Errors, TEXT("\n")).Contains(TEXT("animation-source-content-changed")));
    TestEqual(
        TEXT("Saved non-self wrapper-flag refusal preserves the complete file"), FileHash(File), BeforeWrapperRefusal);
    Input->PinType.bIsUObjectWrapper = OriginalWrapper;
    if (!TestTrue(TEXT("Only the owned non-self wrapper control is restored"), Save(Blueprint)))
        return false;
    UEdGraphPin* AuthoredLabel = nullptr;
    TArray<UEdGraph*> SourceGraphs;
    Blueprint->GetAllGraphs(SourceGraphs);
    for (UEdGraph* Graph : SourceGraphs)
        if (P4AnimOwnership::SourceGraph(Graph))
            for (UEdGraphNode* Node : Graph->Nodes)
                if (P4AnimOwnership::SourceNode(Node))
                    for (UEdGraphPin* Pin : Node->Pins)
                        if (Pin && !Pin->PinFriendlyName.IsEmpty())
                        {
                            FText Label;
                            if (P4AnimOwnership::CanonicalEditorLabel(Pin->PinFriendlyName, Label))
                                AuthoredLabel = Pin;
                        }
    if (!TestNotNull(TEXT("An actual source pin has an engine-shaped display label"), AuthoredLabel))
        return false;
    const FText OriginalLabel = AuthoredLabel->PinFriendlyName;
    const TArray<FText> ChangedLabels = {FText::AsCultureInvariant(OriginalLabel.ToString()),
        FText::Format(FText::FromString(TEXT("{0}")), FText::FromString(OriginalLabel.ToString()))};
    for (int32 Index = 0; Index < ChangedLabels.Num(); ++Index)
    {
        AuthoredLabel->PinFriendlyName = ChangedLabels[Index];
        if (!TestTrue(TEXT("A same-visible-text authored label history is saved"), Save(Blueprint)))
            return false;
        const FString BeforeLabelRefusal = FileHash(File);
        const auto Refusal = UP4BlueprintImport::RunSelectedNative(
            Plans, Out / FString::Printf(TEXT("authored-label-refusal-%d.json"), Index), {Package}, true, {}, true);
        TestFalse(TEXT("Authored invariant/format label identity is refused before replacement"), Refusal.bOk);
        TestTrue(TEXT("Refusal retains the strict saved-text content guard"),
            FString::Join(Refusal.Errors, TEXT("\n")).Contains(TEXT("animation-source-content-changed")));
        TestEqual(TEXT("Authored saved label history refusal preserves the complete file"), FileHash(File),
            BeforeLabelRefusal);
        AuthoredLabel->PinFriendlyName = OriginalLabel;
        if (!TestTrue(TEXT("Only the owned authored-label control edit is restored"), Save(Blueprint)))
            return false;
    }
    UAnimGraphNode_SequencePlayer* Edited = nullptr;
    for (UEdGraph* Graph : SourceGraphs)
        if (P4AnimOwnership::SourceGraph(Graph))
            for (UEdGraphNode* Node : Graph->Nodes)
                if (auto Sequence = Cast<UAnimGraphNode_SequencePlayer>(Node))
                    if (P4AnimOwnership::SourceNode(Sequence))
                    {
                        Edited = Sequence;
                        break;
                    }
    if (!TestNotNull(TEXT("An actual marked source sequence node exists"), Edited))
        return false;
    const float OriginalRate = Edited->Node.PlayRate;
    const float EditedRate = std::nextafter(OriginalRate, TNumericLimits<float>::Max());
    Edited->Node.PlayRate = EditedRate;
    if (!TestTrue(TEXT("A creator's edit to a marked source node is saved"), Save(Blueprint)))
        return false;
    const FString BeforeEditRefusal = FileHash(File);
    const auto EditedRefusal = UP4BlueprintImport::RunSelectedNative(
        Plans, Out / TEXT("edited-source-refusal.json"), {Package}, true, {}, true);
    TestFalse(TEXT("A changed marked source node is refused before replacement"), EditedRefusal.bOk);
    TestTrue(TEXT("Refusal identifies changed animation source content"),
        FString::Join(EditedRefusal.Errors, TEXT("\n")).Contains(TEXT("animation-source-content-changed")));
    TestEqual(TEXT("The complete saved marked-node edit stays byte-identical"), FileHash(File), BeforeEditRefusal);
    TestEqual(TEXT("The authored one-ULP marked-node value survives refusal"), Edited->Node.PlayRate, EditedRate);
    Edited->Node.PlayRate = OriginalRate;
    if (!TestTrue(TEXT("Only the owned marked-node control edit is restored"), Save(Blueprint)))
        return false;
    UAnimGraphNode_StateMachine* SourceMachine = nullptr;
    for (UEdGraphNode* Node : Top->Nodes)
        if (auto Machine = Cast<UAnimGraphNode_StateMachine>(Node))
        {
            SourceMachine = Machine;
            break;
        }
    if (!TestNotNull(TEXT("Actual source state machine exists for a dependent creator control"), SourceMachine))
        return false;
    auto Dependent = NewObject<UK2Node_AnimGetter>(Top, TEXT("KadanCreatorDependent"), RF_Transactional);
    Dependent->SourceNode = SourceMachine;
    Dependent->CreateNewGuid();
    Top->AddNode(Dependent, false, false);
    if (!TestTrue(TEXT("Creator dependency is saved without rewriting source nodes"), Save(Blueprint)))
        return false;
    const FString BeforeRefusal = FileHash(File);
    if (!TestEqual(TEXT("Creator source file has a SHA256 receipt"), BeforeRefusal.Len(), 64))
        return false;
    const auto Refused = UP4BlueprintImport::RunSelectedNative(
        Plans, Out / TEXT("dependent-refusal.json"), {Package}, true, {}, true);
    TestFalse(TEXT("A source-dependent creator node is refused before replacement"), Refused.bOk);
    TestTrue(TEXT("Refusal identifies the creator dependency"),
        FString::Join(Refused.Errors, TEXT("\n")).Contains(TEXT("animation-creator-node-depends-on-source")));
    TestEqual(TEXT("The complete saved creator asset stays byte-identical on refusal"), FileHash(File), BeforeRefusal);
    TestTrue(TEXT("Dependent and independent creator objects remain"),
        Top->Nodes.Contains(Dependent) && Top->Nodes.Contains(Creator) && Dependent->SourceNode == SourceMachine);
    Top->RemoveNode(Dependent);
    auto CreatorGraph = NewObject<UEdGraph>(Blueprint, TEXT("KadanCreatorCrossOuterGraph"), RF_Transactional);
    CreatorGraph->Schema = Top->Schema;
    Top->SubGraphs.Add(CreatorGraph); // independent Outer, but engine removal follows this edge
    if (!TestTrue(TEXT("Cross-outer creator graph is saved"), Save(Blueprint)))
        return false;
    const FString BeforeGraphRefusal = FileHash(File);
    const auto GraphRefused = UP4BlueprintImport::RunSelectedNative(
        Plans, Out / TEXT("cross-outer-refusal.json"), {Package}, true, {}, true);
    TestFalse(TEXT("Unmarked referenced child graph is refused before replacement"), GraphRefused.bOk);
    TestTrue(TEXT("Refusal names the graph ownership boundary"),
        FString::Join(GraphRefused.Errors, TEXT("\n")).Contains(TEXT("animation-creator-graph-in-source-subtree")));
    TestEqual(
        TEXT("Cross-outer creator graph refusal preserves the entire saved file"), FileHash(File), BeforeGraphRefusal);
    TestTrue(TEXT("Independent-outer creator graph reference survives refusal"),
        CreatorGraph->GetOuter() == Blueprint && Top->SubGraphs.Contains(CreatorGraph));
    UE_LOG(LogTemp, Display,
        TEXT(
            "Kadan animation ownership actual-source control: graphs=%d nodes=%d classbytes=%d source_repeats=2 creator_repeats=2 independentcreator=3 eventwirerefusal=1 dependentrefusal=1 crossouterrefusal=1"),
        Baseline.Graphs, Baseline.Nodes, Baseline.Size);
    return !HasAnyErrors();
}
#endif
