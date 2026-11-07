#include "P4BlueprintImport.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
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
bool WriteDispatcherJson(const FString& File, const TSharedRef<FJsonObject>& Object)
{
    FString Text;
    return FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR>::Create(&Text)) &&
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) &&
        FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
// One Blueprint with one event dispatcher (one int parameter), as the game's GQuestAreaSphere has.
struct FDispatcherFixture
{
    const FString Key = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Package = TEXT("/Game/P4BlueprintProof/Dispatcher_") + Key + TEXT("/Area");
    const FString Object = Package + TEXT(".Area");
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("P4BlueprintProof/Dispatcher");
    ~FDispatcherFixture()
    {
        if (UPackage* Made = FindPackage(nullptr, *Package)) P4FixtureRetirement::Package(Made);
        FString File;
        if (FPackageName::TryConvertLongPackageNameToFilename(Package, File, TEXT(".uasset"))) IFileManager::Get().Delete(*File, false, true);
        IFileManager::Get().Delete(*(Directory / TEXT("import") / (Package.Mid(1) + TEXT(".json"))), false, true);
        IFileManager::Get().Delete(*(Directory / TEXT("index.json")), false, true);
    }
    bool Write() const
    {
        auto Plan = MakeShared<FJsonObject>();
        Plan->SetStringField(TEXT("format"), TEXT("p4-blueprint-plan")); Plan->SetNumberField(TEXT("version"), 1);
        Plan->SetStringField(TEXT("package"), Package); Plan->SetStringField(TEXT("name"), TEXT("Area"));
        Plan->SetStringField(TEXT("kind"), TEXT("Blueprint")); Plan->SetStringField(TEXT("parent"), TEXT("/Script/Carrot4.GActor"));
        Plan->SetStringField(TEXT("class_flags"), TEXT("0x0"));
        for (const TCHAR* Field : {TEXT("interfaces"), TEXT("assets"), TEXT("components"), TEXT("overrides"), TEXT("graphs")})
            Plan->SetArrayField(Field, {});
        auto Int = MakeShared<FJsonObject>(); Int->SetStringField(TEXT("c"), TEXT("int"));
        auto Input = MakeShared<FJsonObject>(); Input->SetStringField(TEXT("name"), TEXT("Count")); Input->SetObjectField(TEXT("type"), Int);
        auto Type = MakeShared<FJsonObject>(); Type->SetStringField(TEXT("c"), TEXT("mcdelegate"));
        Type->SetStringField(TEXT("f"), Object + TEXT("_C.NewEventDispatcher_0__DelegateSignature"));
        auto Field = MakeShared<FJsonObject>(); Field->SetStringField(TEXT("name"), TEXT("NewEventDispatcher_0"));
        Field->SetStringField(TEXT("flags"), TEXT("0x100010010005")); Field->SetObjectField(TEXT("type"), Type);
        Field->SetArrayField(TEXT("inputs"), {MakeShared<FJsonValueObject>(Input)});
        Plan->SetArrayField(TEXT("variables"), {MakeShared<FJsonValueObject>(Field)});
        auto Defaults = MakeShared<FJsonObject>(); Defaults->SetArrayField(TEXT("subobjects"), {}); Defaults->SetArrayField(TEXT("values"), {});
        Plan->SetObjectField(TEXT("class_defaults"), Defaults);
        auto Index = MakeShared<FJsonObject>(); Index->SetStringField(TEXT("format"), TEXT("p4-blueprint-plans"));
        Index->SetNumberField(TEXT("version"), 1);
        auto Row = MakeShared<FJsonObject>();
        for (const TCHAR* Name : {TEXT("package"), TEXT("parent"), TEXT("kind")}) Row->SetStringField(Name, Plan->GetStringField(Name));
        Index->SetArrayField(TEXT("order"), {MakeShared<FJsonValueObject>(Row)});
        return WriteDispatcherJson(Directory / TEXT("import") / (Package.Mid(1) + TEXT(".json")), Plan) &&
            WriteDispatcherJson(Directory / TEXT("index.json"), Index);
    }
    bool Run(FAutomationTestBase& Test, bool Reimport) const
    {
        const FString Text = UP4BlueprintImport::Run(Directory, FString(), TEXT("Blueprint"), FString(), 0, Reimport);
        TSharedPtr<FJsonObject> Reply; bool Ok = false;
        FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Reply);
        if (!Reply || !Reply->TryGetBoolField(TEXT("ok"), Ok) || !Ok || Text.Contains(TEXT("dispatcher"))) Test.AddInfo(Text.Left(16384));
        return Test.TestTrue(Reimport ? TEXT("the in-place reimport succeeds") : TEXT("the import succeeds"), Ok) &&
            Test.TestFalse(TEXT("no dispatcher problem is reported"), Text.Contains(TEXT("dispatcher NewEventDispatcher_0")));
    }
    // One dispatcher: one variable, one signature graph and one compiled property with its parameter.
    bool Check(FAutomationTestBase& Test, const TCHAR* Stage) const
    {
        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Object);
        if (!Test.TestTrue(FString(Stage) + TEXT(": the Blueprint and its class exist"), Blueprint && Blueprint->GeneratedClass)) return false;
        int32 Variables = 0, Graphs = 0;
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables) Variables += Variable.VarName == TEXT("NewEventDispatcher_0");
        for (const UEdGraph* Graph : Blueprint->DelegateSignatureGraphs) Graphs += Graph && Graph->GetFName() == TEXT("NewEventDispatcher_0");
        Test.AddInfo(FString::Printf(TEXT("%s: %d variables, %d signature graphs (%d in all)"), Stage, Variables, Graphs, Blueprint->DelegateSignatureGraphs.Num()));
        Test.TestEqual(FString(Stage) + TEXT(": one dispatcher variable"), Variables, 1);
        Test.TestEqual(FString(Stage) + TEXT(": one signature graph"), Graphs, 1);
        Test.TestEqual(FString(Stage) + TEXT(": no other signature graph"), Blueprint->DelegateSignatureGraphs.Num(), 1);
        FMulticastDelegateProperty* Property = FindFProperty<FMulticastDelegateProperty>(Blueprint->GeneratedClass, TEXT("NewEventDispatcher_0"));
        UFunction* Signature = Property ? Property->SignatureFunction : nullptr;
        return Test.TestTrue(FString(Stage) + TEXT(": the compiled dispatcher takes its int parameter"),
            Signature && FindFProperty<FIntProperty>(Signature, TEXT("Count")) != nullptr);
    }
};
}

// The game's Blueprints with event dispatchers (GQuestAreaSphere's NewEventDispatcher_0) imported again over
// themselves, as a normal import of a map does when the project already has them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintDispatcherReimportTest, "P4Blueprints.DispatcherReimport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintDispatcherReimportTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("genuine game declaration plugin is loaded"), FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GActor")))) return false;
    FDispatcherFixture Fixture;
    if (!TestTrue(TEXT("the plan is written"), Fixture.Write()) || !Fixture.Run(*this, false) || !Fixture.Check(*this, TEXT("import"))) return false;
    if (!Fixture.Run(*this, true) || !Fixture.Check(*this, TEXT("first reimport"))) return false;
    Fixture.Run(*this, true) && Fixture.Check(*this, TEXT("second reimport"));
    return !HasAnyErrors();
}
#endif
