#include "P4BlueprintImport.h"
#include "P4OriginalImportSave.h"
// This test uses the Kadan importer, so only the Kadan test module builds it.
#if defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4KadanImport.h"
#endif
#include "Engine/DataTable.h"
#include "UObject/StructOnScope.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4FixtureRetirement { bool Package(UPackage* Made); }
namespace
{
TSharedRef<FJsonObject> InheritedValue(const TCHAR* Name, const TCHAR* Text)
{
    auto Value = MakeShared<FJsonObject>();
    Value->SetStringField(TEXT("name"), Name); Value->SetNumberField(TEXT("index"), 0);
    Value->SetStringField(TEXT("text"), Text); return Value;
}
bool WriteInheritanceJson(const FString& File, const TSharedRef<FJsonObject>& Object)
{
    FString Text;
    return FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR>::Create(&Text)) &&
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) &&
        FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
struct FInheritanceFixture
{
    const FString Key = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Root = TEXT("/Game/P4BlueprintProof/Inheritance_") + Key;
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("P4BlueprintProof/Inheritance");
    FString Package(const TCHAR* Name) const { return Root / Name; }
    FString Object(const TCHAR* Name) const { return Package(Name) + TEXT(".") + Name; }
    ~FInheritanceFixture()
    {
        // Reuse the private test module's exact Blueprint/class/CDO retirement.
        for (const TCHAR* Name : {TEXT("Child"), TEXT("Parent")})
        {
            if (UPackage* Made = FindPackage(nullptr, *Package(Name))) P4FixtureRetirement::Package(Made);
            FString File;
            if (FPackageName::TryConvertLongPackageNameToFilename(Package(Name), File, TEXT(".uasset")))
                IFileManager::Get().Delete(*File, false, true);
            const FString PlanFile = Directory / TEXT("import") / (Package(Name).Mid(1) + TEXT(".json"));
            IFileManager::Get().Delete(*PlanFile, false, true);
        }
        IFileManager::Get().Delete(*(Directory / TEXT("index.json")), false, true);
    }
    TSharedRef<FJsonObject> Plan(const TCHAR* Name, const FString& Parent, const TCHAR* Variable, const TCHAR* Count,
        const TCHAR* Context) const
    {
        auto Plan = MakeShared<FJsonObject>();
        Plan->SetStringField(TEXT("format"), TEXT("p4-blueprint-plan")); Plan->SetNumberField(TEXT("version"), 1);
        Plan->SetStringField(TEXT("package"), Package(Name)); Plan->SetStringField(TEXT("name"), Name);
        Plan->SetStringField(TEXT("kind"), TEXT("Blueprint")); Plan->SetStringField(TEXT("parent"), Parent);
        Plan->SetStringField(TEXT("class_flags"), TEXT("0x0"));
        for (const TCHAR* Field : {TEXT("interfaces"), TEXT("assets"), TEXT("components"), TEXT("overrides"), TEXT("graphs")})
            Plan->SetArrayField(Field, {});
        auto Type = MakeShared<FJsonObject>(); Type->SetStringField(TEXT("c"), TEXT("int"));
        auto Field = MakeShared<FJsonObject>(); Field->SetStringField(TEXT("name"), Variable);
        Field->SetStringField(TEXT("flags"), TEXT("0x5")); Field->SetObjectField(TEXT("type"), Type);
        Plan->SetArrayField(TEXT("variables"), {MakeShared<FJsonValueObject>(Field)});
        auto Defaults = MakeShared<FJsonObject>(); Defaults->SetArrayField(TEXT("subobjects"), {});
        Defaults->SetArrayField(TEXT("values"), {MakeShared<FJsonValueObject>(InheritedValue(Variable, Count)),
            MakeShared<FJsonValueObject>(InheritedValue(TEXT("WaterBoxFollowContext"), Context))});
        Plan->SetObjectField(TEXT("class_defaults"), Defaults); return Plan;
    }
    bool Write() const
    {
        const TArray<TSharedRef<FJsonObject>> Plans = {
            Plan(TEXT("Parent"), TEXT("/Script/Carrot4.GActor"), TEXT("ParentCount"), TEXT("31"), TEXT("(bCheck=True,Radius=13)")),
            Plan(TEXT("Child"), Object(TEXT("Parent")) + TEXT("_C"), TEXT("ChildCount"), TEXT("7"), TEXT("(Radius=27)"))};
        auto Index = MakeShared<FJsonObject>(); Index->SetStringField(TEXT("format"), TEXT("p4-blueprint-plans"));
        Index->SetNumberField(TEXT("version"), 1); TArray<TSharedPtr<FJsonValue>> Order;
        for (const auto& Plan : Plans)
        {
            auto Row = MakeShared<FJsonObject>();
            for (const TCHAR* Field : {TEXT("package"), TEXT("parent"), TEXT("kind")}) Row->SetStringField(Field, Plan->GetStringField(Field));
            Order.Add(MakeShared<FJsonValueObject>(Row));
            if (!WriteInheritanceJson(Directory / TEXT("import") / (Plan->GetStringField(TEXT("package")).Mid(1) + TEXT(".json")), Plan)) return false;
        }
        Index->SetArrayField(TEXT("order"), Order); return WriteInheritanceJson(Directory / TEXT("index.json"), Index);
    }
    bool Run(FAutomationTestBase& Test, bool Reimport) const
    {
        const FString Text = UP4BlueprintImport::Run(Directory, FString(), TEXT("Blueprint"), FString(), 0, Reimport);
        TSharedPtr<FJsonObject> Reply; bool Ok = false;
        FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Reply);
        if (!Reply || !Reply->TryGetBoolField(TEXT("ok"), Ok) || !Ok) Test.AddInfo(Text.Left(16384));
        return Test.TestTrue(Reimport ? TEXT("normal in-place reimport succeeds") : TEXT("normal parent/child batch import succeeds"), Ok);
    }
};
bool CheckInheritedContext(FAutomationTestBase& Test, UObject* Defaults, float ExpectedRadius, const TCHAR* Stage)
{
    FStructProperty* Context = Defaults ? FindFProperty<FStructProperty>(Defaults->GetClass(), TEXT("WaterBoxFollowContext")) : nullptr;
    FBoolProperty* Check = Context ? FindFProperty<FBoolProperty>(Context->Struct, TEXT("bCheck")) : nullptr;
    FFloatProperty* Radius = Context ? FindFProperty<FFloatProperty>(Context->Struct, TEXT("Radius")) : nullptr;
    if (!Test.TestTrue(TEXT("the genuine native context fields exist"), Check && Radius)) return false;
    void* Value = Context->ContainerPtrToValuePtr<void>(Defaults);
    Test.AddInfo(FString::Printf(TEXT("%s context bCheck=%d radius=%g"), Stage,
        int32(Check->GetPropertyValue_InContainer(Value)), Radius->GetPropertyValue_InContainer(Value)));
    Test.TestTrue(FString(Stage) + TEXT(" omitted bCheck inherits the completed parent's true value"), Check->GetPropertyValue_InContainer(Value));
    Test.TestEqual(TEXT("the explicitly authored radius wins"), Radius->GetPropertyValue_InContainer(Value), ExpectedRadius);
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintFreshInheritanceTest, "P4Blueprints.FreshParentDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintFreshInheritanceTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("genuine game declaration plugin is loaded"), FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GActor")))) return false;
    FInheritanceFixture Fixture;
    if (!TestTrue(TEXT("owned parent/child plans are written"), Fixture.Write()) || !Fixture.Run(*this, false)) return false;
    UBlueprint* Parent = LoadObject<UBlueprint>(nullptr, *Fixture.Object(TEXT("Parent")));
    UBlueprint* Child = LoadObject<UBlueprint>(nullptr, *Fixture.Object(TEXT("Child")));
    if (!TestTrue(TEXT("both generated classes exist"), Parent && Parent->GeneratedClass && Child && Child->GeneratedClass)) return false;
    UObject* ParentDefaults = Parent->GeneratedClass->GetDefaultObject(false);
    UObject* ChildDefaults = Child->GeneratedClass->GetDefaultObject(false);
    if (!CheckInheritedContext(*this, ParentDefaults, 13.f, TEXT("fresh parent")) || !CheckInheritedContext(*this, ChildDefaults, 27.f, TEXT("fresh child"))) return false;
    FIntProperty* Inherited = FindFProperty<FIntProperty>(Child->GeneratedClass, TEXT("ParentCount"));
    FIntProperty* Own = FindFProperty<FIntProperty>(Child->GeneratedClass, TEXT("ChildCount"));
    if (!TestTrue(TEXT("inherited and child-owned Blueprint variables exist"), Inherited && Own)) return false;
    TestEqual(TEXT("completed parent Blueprint variable is inherited"), Inherited->GetPropertyValue_InContainer(ChildDefaults), 31);
    TestEqual(TEXT("child Blueprint variable keeps its authored value"), Own->GetPropertyValue_InContainer(ChildDefaults), 7);
    FFloatProperty* Creator = FindFProperty<FFloatProperty>(Child->GeneratedClass, TEXT("InitialLifeSpan"));
    if (!TestNotNull(TEXT("the fixture has a field outside the source plan"), Creator)) return false;
    Creator->SetPropertyValue_InContainer(ChildDefaults, 55.f);
    FString File;
    if (!TestTrue(TEXT("synthetic annotation owns the exact unique imported fixture"), Child->GetPathName() == Fixture.Object(TEXT("Child")) &&
        FPackageName::TryConvertLongPackageNameToFilename(Fixture.Package(TEXT("Child")), File, TEXT(".uasset")))) return false;
    {
        // Imported source is readonly even for these synthetic plans. Reuse the
        // existing private reimport fixture's exact native annotation publication;
        // this is not an ordinary creator save or a product permission toggle.
        P4ImportPublication::FImportScope Import;
        P4ImportPublication::FSaveScope Save(Child->GetOutermost());
        if (!TestTrue(TEXT("test-only non-source annotation is published before reimport"),
            UPackage::SavePackage(Child->GetOutermost(), Child, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError))) return false;
    }
    TestTrue(TEXT("source annotation restores native publication scope"), !P4ImportPublication::IsImporting() && !P4ImportPublication::IsSaving(Child->GetOutermost()));
    TestTrue(TEXT("source annotation restores readonly file protection"), IFileManager::Get().IsReadOnly(*File));
    Child->GetOutermost()->SetDirtyFlag(false);
    if (!Fixture.Run(*this, true)) return false;
    Child = LoadObject<UBlueprint>(nullptr, *Fixture.Object(TEXT("Child")));
    if (!TestTrue(TEXT("reimport retained the child class"), Child && Child->GeneratedClass)) return false;
    ChildDefaults = Child->GeneratedClass->GetDefaultObject(false);
    Creator = FindFProperty<FFloatProperty>(Child->GeneratedClass, TEXT("InitialLifeSpan"));
    if (!TestTrue(TEXT("reimport retained creator field and CDO"), Creator && ChildDefaults)) return false;
    TestEqual(TEXT("fresh inheritance refresh does not erase saved non-source reimport values"), Creator->GetPropertyValue_InContainer(ChildDefaults), 55.f);
    CheckInheritedContext(*this, ChildDefaults, 27.f, TEXT("reimport child"));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintDeclarationBoundaryTest, "P4Blueprints.DeclarationDependencies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintDeclarationBoundaryTest::RunTest(const FString&)
{
    FInheritanceFixture Fixture;
    if (!TestTrue(TEXT("normal parent/child source plans exist"), Fixture.Write())) return false;
    UScriptStruct* Schema = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.CameraParameter"));
    FStructProperty* Look = Schema ? FindFProperty<FStructProperty>(Schema, TEXT("LookOverParam")) : nullptr;
    FStructProperty* Drone = Look ? FindFProperty<FStructProperty>(Look->Struct, TEXT("DroneParameter")) : nullptr;
    FClassProperty* Pin = Drone ? FindFProperty<FClassProperty>(Drone->Struct, TEXT("DRONEPIN")) : nullptr;
    if (!TestTrue(TEXT("genuine camera table has its native class-typed column"), Schema && Look && Drone && Pin && Pin->MetaClass)) return false;
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage())); Table->RowStruct = Schema;
    bool Called = false;
    const auto PopulateTable = [&](const TMap<FName, UClass*>& Declared, FString& Error)
    {
        Called = true;
        UClass* Parent = Declared.FindRef(FName(*Fixture.Package(TEXT("Parent"))));
        UClass* Child = Declared.FindRef(FName(*Fixture.Package(TEXT("Child"))));
        FIntProperty* Count = Parent ? FindFProperty<FIntProperty>(Parent, TEXT("ParentCount")) : nullptr;
        if (!Parent || !Child || !Count || !Child->IsChildOf(Pin->MetaClass))
        { Error = TEXT("actual declared classes do not match the genuine native table column"); return false; }
        TestEqual(TEXT("table phase occurs before parent source defaults"), Count->GetPropertyValue_InContainer(Parent->GetDefaultObject(false)), 0);
        FStructOnScope Row(Schema);
        void* LookValue = Look->ContainerPtrToValuePtr<void>(Row.GetStructMemory());
        void* DroneValue = Drone->ContainerPtrToValuePtr<void>(LookValue);
        Pin->SetObjectPropertyValue_InContainer(DroneValue, Child);
        Table->AddRow(TEXT("Declared"), *reinterpret_cast<FTableRowBase*>(Row.GetStructMemory()));
        return true;
    };
    const auto Result = UP4BlueprintImport::RunSelectedNative(Fixture.Directory, FString(),
        {Fixture.Package(TEXT("Parent")), Fixture.Package(TEXT("Child"))}, false, PopulateTable);
    TestTrue(TEXT("normal native batch completes with its declaration dependency callback"), Result.bOk);
    TestTrue(TEXT("the actual declaration callback ran"), Called);
    TestEqual(TEXT("both source rows actually complete"), Result.CompletedPackages.Num(), 2);
    UBlueprint* Child = LoadObject<UBlueprint>(nullptr, *Fixture.Object(TEXT("Child")));
    UBlueprint* Parent = LoadObject<UBlueprint>(nullptr, *Fixture.Object(TEXT("Parent")));
    if (!TestTrue(TEXT("completed source classes remain actual Blueprint classes"), Parent && Child && Parent->GeneratedClass && Child->GeneratedClass)) return false;
    TestFalse(TEXT("complete saved parent is not marked incomplete"), P4BlueprintReconstructionIncomplete(Parent));
    TestFalse(TEXT("complete saved child class is not marked incomplete"), P4BlueprintReconstructionIncomplete(Child->GeneratedClass));
    FIntProperty* ParentCount = FindFProperty<FIntProperty>(Parent->GeneratedClass, TEXT("ParentCount"));
    TestTrue(TEXT("source defaults apply after table population"), ParentCount && ParentCount->GetPropertyValue_InContainer(Parent->GeneratedClass->GetDefaultObject(false)) == 31);
    uint8* const* Found = Table->GetRowMap().Find(TEXT("Declared"));
    void* LookValue = Found ? Look->ContainerPtrToValuePtr<void>(*Found) : nullptr;
    void* DroneValue = LookValue ? Drone->ContainerPtrToValuePtr<void>(LookValue) : nullptr;
    TestTrue(TEXT("native table retains the genuine current generated class after value compilation"),
        DroneValue && Pin->GetObjectPropertyValue_InContainer(DroneValue) == Child->GeneratedClass);
    Table->EmptyTable();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintDeclarationFailureTest, "P4Blueprints.DeclarationDependencyFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintDeclarationFailureTest::RunTest(const FString&)
{
    FInheritanceFixture Fixture;
    if (!TestTrue(TEXT("normal source plans exist"), Fixture.Write())) return false;
    bool Called = false;
    const auto Refuse = [&](const TMap<FName, UClass*>& Declared, FString& Error)
    {
        Called = true;
        TestEqual(TEXT("failure happens after both genuine class declarations"), Declared.Num(), 2);
        Error = TEXT("phase-fixture-dependency-refused"); return false;
    };
    const auto Result = UP4BlueprintImport::RunSelectedNative(Fixture.Directory, FString(),
        {Fixture.Package(TEXT("Parent")), Fixture.Package(TEXT("Child"))}, false, Refuse);
    TestTrue(TEXT("the failure callback ran"), Called);
    TestFalse(TEXT("callback refusal fails the native batch"), Result.bOk);
    TestTrue(TEXT("native failure retains the exact dependency reason"), FString::Join(Result.Errors, TEXT("\n")).Contains(TEXT("phase-fixture-dependency-refused")));
    TestEqual(TEXT("class declaration is not source-value completion"), Result.CompletedPackages.Num(), 0);
    TestFalse(TEXT("ordinary source reconstruction refusal is not infrastructure failure"), Result.bInfrastructureFailure);
    UBlueprint* Child = FindObject<UBlueprint>(nullptr, *Fixture.Object(TEXT("Child")));
    TestTrue(TEXT("a genuine class still exists after the named refusal"), Child && Child->GeneratedClass);
    TestTrue(TEXT("failed source reconstruction is marked incomplete"), P4BlueprintReconstructionIncomplete(Child));
    TestTrue(TEXT("its generated class carries the same incomplete-source role"), Child && P4BlueprintReconstructionIncomplete(Child->GeneratedClass));
    TArray<TSharedPtr<FP4KadanSourceAsset>> Selected;
    for (const TCHAR* Name : {TEXT("Parent"), TEXT("Child")})
    { auto Asset = MakeShared<FP4KadanSourceAsset>(); Asset->Package = Fixture.Package(Name); Asset->Kind = TEXT("Blueprint"); Selected.Add(Asset); }
    const auto MapResult = P4KadanBlueprintBatchResult(Result, Selected);
    TestFalse(TEXT("the actual map batch admission also fails"), MapResult.Ok);
    TestEqual(TEXT("the map admits no incomplete declared class"), MapResult.Imported.Num(), 0);
    TestEqual(TEXT("both incomplete declarations are named in the map result"), MapResult.NotImported.Num(), 2);
    TestTrue(TEXT("the map preserves the named native failure"), FString::Join(MapResult.Problems, TEXT("\n")).Contains(TEXT("phase-fixture-dependency-refused")));
    TSet<FName> VerifiedGameSources;
    for (const auto& Asset : Selected) VerifiedGameSources.Add(FName(*Asset->Package));
    const auto KeptResult = P4KadanBlueprintBatchResult(Result, Selected, VerifiedGameSources);
    TestTrue(TEXT("actual failed native batch can retain caller-verified original game references"), KeptResult.Ok);
    TestEqual(TEXT("retained references never claim those incomplete class declarations were imported"), KeptResult.Imported.Num(), 0);
    TestEqual(TEXT("both incomplete rows remain original game references"), KeptResult.ReferenceOnly.Num(), 2);
    FP4BlueprintImportRunResult ClaimedComplete;
    ClaimedComplete.bOk=true; ClaimedComplete.Phase=TEXT("done");
    for (const auto& Asset:Selected) ClaimedComplete.CompletedPackages.Add(FName(*Asset->Package));
    const auto Resume=P4KadanBlueprintBatchResult(ClaimedComplete,Selected);
    TestFalse(TEXT("the next resume cannot admit failed generated shells from their presence alone"), Resume.Ok);
    TestEqual(TEXT("no marked shell becomes a resumed reconstructed asset"), Resume.Imported.Num(), 0);
    UPackage* CreatorPackage=CreatePackage(*Fixture.Package(TEXT("UntouchedCreator")));
    UBlueprint* Creator=NewObject<UBlueprint>(CreatorPackage,TEXT("UntouchedCreator"),RF_Public|RF_Standalone);
    TestFalse(TEXT("ordinary untouched creator Blueprint is unaffected"), P4BlueprintReconstructionIncomplete(Creator));
    P4FixtureRetirement::Package(CreatorPackage);
    return !HasAnyErrors();
}
#endif
