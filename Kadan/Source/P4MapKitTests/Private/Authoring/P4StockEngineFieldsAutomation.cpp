#if P4_MAPKIT_TESTS
#include "P4StockEngineFieldContent.h"
#include "P4DiagnosticCookPackageGuard.h"
#include "P4GroundTypes.h"
#include "P4MapKitCore/cooked_export_rebuild.h"
#include "Landscape.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "InstancedFoliageActor.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4FoliageRelocationTest, "P4MapKit.EngineFields.FoliageCookRelocation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4FoliageRelocationTest::RunTest(const FString&)
{
    const FString Name = TEXT("FoliageRelocation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*(TEXT("/Game/Mods/") + Name));
    TStrongObjectPtr<UWorld> World(UWorld::CreateWorld(EWorldType::Editor, false, FName(*Name), Package));
    if (!TestNotNull(TEXT("The native fixture world exists"), World.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        Package->SetDirtyFlag(false);
    };
    AInstancedFoliageActor* Actor = World->SpawnActor<AInstancedFoliageActor>();
    auto* Type = NewObject<UFoliageType_InstancedStaticMesh>(Actor, TEXT("NativeFoliage"));
    Type->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    FFoliageInfo* Info = Actor->AddMesh(Type);
    if (!TestNotNull(TEXT("Real native foliage info is created"), Info))
        return false;
    FFoliageInstance Instance;
    Instance.Location = FVector(125, 250, 375);
    Info->AddInstance(Actor, Type, Instance);
    TestEqual(TEXT("The native fixture contains a placed instance"), Info->GetComponent()->GetInstanceCount(), 1);
    const FString Base = FPaths::ProjectSavedDir() / TEXT("P4MapKit/StockEngineFields/Foliage");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Base), true);
    ITargetPlatform* Target = GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("WindowsNoEditor"));
    FString Error;
    {
        FP4DiagnosticCookPackageGuard Guard(Package);
        if (!TestTrue(TEXT("The foliage cook is guarded"), Guard.IsReady()))
            return false;
        Package->SetPackageFlags(PKG_FilterEditorOnly);
        if (!TestTrue(TEXT("The real foliage world cooks"),
                GEditor->SavePackage(Package, World.Get(), RF_Public | RF_Standalone, *(Base + TEXT(".umap")), GError,
                    nullptr, false, true, SAVE_KeepGUID | SAVE_KeepDirty | SAVE_Unversioned | SAVE_NoError | SAVE_Async,
                    Target, FDateTime::MinValue(), false)) ||
            !TestTrue(TEXT("The native cook finishes and restores state"), Guard.AwaitAndRestore(Error)))
            return false;
    }
    TArray<uint8> H, B;
    if (!FFileHelper::LoadFileToArray(H, *(Base + TEXT(".umap"))) ||
        !FFileHelper::LoadFileToArray(B, *(Base + TEXT(".uexp"))))
        return false;
    std::vector<uint8> Header(H.GetData(), H.GetData() + H.Num()), Body(B.GetData(), B.GetData() + B.Num());
    if (!TestTrue(TEXT("Fully decoded static foliage native data can relocate"),
            P4ValidateStockEngineCookRelocation(Header, Body, Error)))
        AddError(Error);
    const auto Native = P4MapKit::ReadPackage(Header, Body);
    bool ExercisedUnknownTail = false;
    for (std::size_t Index = 0; Index < Native.Exports.size(); ++Index)
    {
        const auto& Export = Native.Exports[Index];
        std::string Class;
        if (!Native.Reference(Export.Class, Class) || Class != "/Script/Foliage.InstancedFoliageActor")
            continue;
        std::vector<uint8> Unknown(Body.begin() + Export.Offset, Body.begin() + Export.Offset + Export.Size);
        Unknown.push_back(0xaa);
        P4MapKit::Export::RewrittenPackage Broken;
        std::string Why;
        if (!TestTrue(TEXT("The foliage negative control retains a valid package around an unknown native tail"),
                P4MapKit::Export::RebuildCookedExports(Header, Body, Native.Names, {{Index, Unknown}}, Broken, Why)))
            return false;
        const auto BeforeHeader = Broken.Header, BeforeBody = Broken.Body;
        TestFalse(TEXT("Partially decoded foliage refuses relocation"),
            P4ValidateStockEngineCookRelocation(Broken.Header, Broken.Body, Error));
        TestTrue(
            TEXT("A refusal preserves both input buffers"), BeforeHeader == Broken.Header && BeforeBody == Broken.Body);
        ExercisedUnknownTail = true;
    }
    TestTrue(TEXT("The fixture actually cooked a native foliage actor export"), ExercisedUnknownTail);
    TestEqual(TEXT("Validation leaves the placed foliage intact"), Info->GetComponent()->GetInstanceCount(), 1);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4StockEngineFieldsCookTest, "P4MapKit.EngineFields.StockStorageAndCook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4StockEngineFieldsCookTest::RunTest(const FString&)
{
    const FString PackageName = TEXT("/Game/Mods/StockFields_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*PackageName);
    TStrongObjectPtr<UStaticMeshComponent> Component(
        NewObject<UStaticMeshComponent>(Package, TEXT("Component"), RF_Public | RF_Standalone));
    ON_SCOPE_EXIT
    {
        Component->ClearFlags(RF_Public | RF_Standalone);
        Component->MarkPendingKill();
        Package->SetDirtyFlag(false);
    };
    FP4StockEngineField Field;
    Field.Name = TEXT("C4MaterialMeshType");
    Field.Integer = 10;
    FString Error;
    if (!TestTrue(TEXT("A stock component stores the captured integer"),
            P4SetStockEngineField(Component.Get(), Field, Error)))
    {
        AddError(Error);
        return false;
    }
    TestNull(TEXT("No bytes or reflected properties are added to the engine class"),
        Component->GetClass()->FindPropertyByName(Field.Name));
    TestEqual(TEXT("The editor record reads the exact value"), P4GetStockEngineFields(Component.Get())[0].Integer, 10);
    // The game's stencil choice, as its file names it, is held on a stock component.
    FP4StockEngineField Stencil;
    Stencil.Name = TEXT("C4Stencil");
    Stencil.Choice = TEXT("EC4Stencil::EC4S_1");
    if (!TestTrue(TEXT("A stock component stores the game's stencil choice"),
            P4SetStockEngineField(Component.Get(), Stencil, Error)))
        AddError(Error);
    TestTrue(TEXT("A decoded stencil choice is stored"),
        P4StoreDecodedStockEngineField(Component.Get(), TEXT("C4Stencil"),
            P4MapKit::FDecodedValue::Atom(P4MapKit::FDecodedValue::EKind::String, "EC4Stencil::EC4S_1"), Error));
    TStrongObjectPtr<UStaticMeshComponent> Copy(
        DuplicateObject<UStaticMeshComponent>(Component.Get(), GetTransientPackage()));
    Field.Integer = 12;
    TestTrue(TEXT("A duplicated component edits its own record"), P4SetStockEngineField(Copy.Get(), Field, Error));
    TestEqual(
        TEXT("Editing a copy leaves its source unchanged"), P4GetStockEngineFields(Component.Get())[0].Integer, 10);
    auto Bad = MakeShared<FJsonObject>();
    Bad->SetStringField(TEXT("name"), TEXT("C4MaterialMeshType"));
    Bad->SetStringField(TEXT("type"), TEXT("FloatProperty"));
    Bad->SetNumberField(TEXT("index"), 0);
    Bad->SetStringField(TEXT("text"), TEXT("99"));
    TestTrue(TEXT("A mismatched source type is refused"),
        P4ApplyStockEngineFieldPlan(Component.Get(), Bad, Error) == EP4StockEngineField::Refused);
    TestEqual(TEXT("A refused source value leaves the record unchanged"),
        P4GetStockEngineFields(Component.Get())[0].Integer, 10);
    Bad->SetStringField(TEXT("type"), TEXT("IntProperty"));
    Bad->SetStringField(TEXT("text"), TEXT("4294967306"));
    TestTrue(TEXT("An overflowing integer is refused rather than wrapped"),
        P4ApplyStockEngineFieldPlan(Component.Get(), Bad, Error) == EP4StockEngineField::Refused);
    TestEqual(TEXT("An overflowing source value leaves the record unchanged"),
        P4GetStockEngineFields(Component.Get())[0].Integer, 10);
    const FString Base = FPaths::ProjectSavedDir() / TEXT("P4MapKit/StockEngineFields/Component");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Base), true);
    {
        UPackage* InstancePackage = CreatePackage(*(PackageName + TEXT("_Instance")));
        TStrongObjectPtr<UStaticMeshComponent> Instance(
            NewObject<UStaticMeshComponent>(InstancePackage, TEXT("Component"), RF_Public | RF_Standalone));
        ON_SCOPE_EXIT
        {
            Instance->ClearFlags(RF_Public | RF_Standalone);
            Instance->MarkPendingKill();
            InstancePackage->SetDirtyFlag(false);
        };
        auto* SourceData = Cast<IInterface_AssetUserData>(Component.Get())
                               ->GetAssetUserDataOfClass(UP4StockEngineFields::StaticClass());
        Cast<IInterface_AssetUserData>(Instance.Get())->AddAssetUserData(SourceData);
        TestTrue(TEXT("The fixture reproduces a reference to another template's private data"),
            SourceData->GetOutermost() != InstancePackage);
        TestTrue(TEXT("The explicit import boundary gives the instance its own data"),
            P4OwnStockEngineFields(InstancePackage, Error));
        auto* OwnedData =
            Cast<IInterface_AssetUserData>(Instance.Get())->GetAssetUserDataOfClass(UP4StockEngineFields::StaticClass());
        TestTrue(TEXT("The saved record belongs to this component, with an independent identity"),
            OwnedData != SourceData && OwnedData->GetOuter() == Instance.Get());
        TestEqual(TEXT("Ownership repair preserves the inherited value"),
            P4GetStockEngineFields(Instance.Get())[0].Integer, 10);
        TestTrue(TEXT("The actual ordinary editor save accepts the owned record"),
            GEditor->SavePackage(InstancePackage, Instance.Get(), RF_Public | RF_Standalone,
                *(Base + TEXT("-instance.uasset")), GError, nullptr, false, true,
                SAVE_KeepGUID | SAVE_KeepDirty | SAVE_NoError));
    }
    {
        // A level instance made from a class template whose record was saved private (classes imported
        // before template records were public) names that record as its archetype.
        UPackage* TemplatePackage = CreatePackage(*(PackageName + TEXT("_Class")));
        TStrongObjectPtr<UStaticMeshComponent> Template(NewObject<UStaticMeshComponent>(
            TemplatePackage, TEXT("Template"), RF_Public | RF_Standalone | RF_ArchetypeObject));
        ON_SCOPE_EXIT
        {
            Template->ClearFlags(RF_Public | RF_Standalone);
            Template->MarkPendingKill();
            TemplatePackage->SetDirtyFlag(false);
        };
        Field.Integer = 7;
        if (!TestTrue(TEXT("A class template stores the captured integer"),
                P4SetStockEngineField(Template.Get(), Field, Error)))
        {
            AddError(Error);
            return false;
        }
        auto* TemplateData =
            Cast<IInterface_AssetUserData>(Template.Get())->GetAssetUserDataOfClass(UP4StockEngineFields::StaticClass());
        TestTrue(TEXT("New template records are public like engine component templates"),
            TemplateData->HasAnyFlags(RF_Public));
        TemplateData->ClearFlags(RF_Public);
        // A level instance's archetype is found by name through its outer's archetype, which a class
        // Blueprint provides in the real import (Cave025 is the real control). Here the class package
        // boundary is checked: it repairs a template record saved private by earlier imports.
        TestTrue(TEXT("The class package boundary makes its template record public"),
            P4OwnStockEngineFields(TemplatePackage, Error) && TemplateData->HasAnyFlags(RF_Public));
        TestEqual(
            TEXT("The template keeps its record and value"), P4GetStockEngineFields(Template.Get())[0].Integer, 7);
        TestTrue(TEXT("The template record is still the same object"),
            Cast<IInterface_AssetUserData>(Template.Get())
                    ->GetAssetUserDataOfClass(UP4StockEngineFields::StaticClass()) == TemplateData);
        Field.Integer = 12;
    }
    ITargetPlatform* Target = GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("WindowsNoEditor"));
    if (!TestNotNull(TEXT("The native cook target exists"), Target))
        return false;
    {
        FP4DiagnosticCookPackageGuard Guard(Package);
        if (!TestTrue(TEXT("The cook state is guarded"), Guard.IsReady()))
            return false;
        Package->SetPackageFlags(PKG_FilterEditorOnly);
        const bool Saved = GEditor->SavePackage(Package, Component.Get(), RF_Public | RF_Standalone,
            *(Base + TEXT(".uasset")), GError, nullptr, false, true,
            SAVE_KeepGUID | SAVE_KeepDirty | SAVE_Unversioned | SAVE_NoError | SAVE_Async, Target,
            FDateTime::MinValue(), false);
        if (!TestTrue(TEXT("The actual stock component cooks"), Saved) ||
            !TestTrue(TEXT("The cook restores its live state"), Guard.AwaitAndRestore(Error)))
            return false;
    }
    TArray<uint8> HeaderBytes, BodyBytes;
    if (!TestTrue(TEXT("The native header was written"),
            FFileHelper::LoadFileToArray(HeaderBytes, *(Base + TEXT(".uasset")))) ||
        !TestTrue(
            TEXT("The native exports were written"), FFileHelper::LoadFileToArray(BodyBytes, *(Base + TEXT(".uexp")))))
        return false;
    std::vector<uint8> Header(HeaderBytes.GetData(), HeaderBytes.GetData() + HeaderBytes.Num()),
        Body(BodyBytes.GetData(), BodyBytes.GetData() + BodyBytes.Num());
    {
        const auto Original = P4MapKit::ReadPackage(Header, Body);
        for (std::size_t At = 0; At < Original.Exports.size(); ++At)
            if (Original.Exports[At].Name == "Component")
            {
                const auto& Export = Original.Exports[At];
                std::vector<uint8> Unknown(Body.begin() + Export.Offset, Body.begin() + Export.Offset + Export.Size);
                Unknown.push_back(0xaa);
                P4MapKit::Export::RewrittenPackage Broken;
                std::string Why;
                if (!TestTrue(TEXT("The negative control has a valid package and an unknown native tail"),
                        P4MapKit::Export::RebuildCookedExports(
                            Header, Body, Original.Names, {{At, Unknown}}, Broken, Why)))
                    return false;
                const auto BrokenHeader = Broken.Header, BrokenBody = Broken.Body;
                TestFalse(TEXT("Unknown native data refuses relocation"),
                    P4TranslateStockEngineFieldCook(Package, Broken.Header, Broken.Body, Error));
                TestTrue(TEXT("An unqualified tail preserves both original buffers"),
                    Broken.Header == BrokenHeader && Broken.Body == BrokenBody);
            }
    }
    if (!TestTrue(TEXT("The cook pass emits the actual stored game field"),
            P4TranslateStockEngineFieldCook(Package, Header, Body, Error)))
    {
        AddError(Error);
        return false;
    }
    const auto Native = P4MapKit::ReadPackage(Header, Body);
    bool Found = false;
    for (const auto& Export : Native.Exports)
    {
        if (Export.Name != "Component")
            continue;
        P4MapKit::FPropertyDecoder Decoder(Native, Body);
        P4MapKit::FDecodedValue Values;
        std::size_t Position = Export.Offset;
        TestTrue(TEXT("The actual output decodes"),
            Decoder.Properties(Position, Export.Offset + Export.Size, Values, 0, Export.Name));
        const auto* Value = Values.Field("C4MaterialMeshType");
        Found = Value && Value->Text == "10";
    }
    TestTrue(TEXT("The cooked component contains C4MaterialMeshType=10"), Found);
    for (const auto& Import : Native.Imports)
        TestTrue(TEXT("Editor field records do not ship"),
            Import.Class != "P4StockEngineFields" && Import.Name != "P4StockEngineFields");
    const auto BeforeHeader = Header, BeforeBody = Body;
    TestFalse(TEXT("A second pass refuses to shadow a serialized native field"),
        P4TranslateStockEngineFieldCook(Package, Header, Body, Error));
    TestTrue(TEXT("A refused pass preserves both buffers"), Header == BeforeHeader && Body == BeforeBody);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorLandscapeGroundTest, "P4MapKit.Parity.CreatorLandscapeGroundTags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorLandscapeGroundTest::RunTest(const FString&)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!TestNotNull(TEXT("The editor world exists"), World))
        return false;
    ALandscape* Landscape = World->SpawnActor<ALandscape>();
    if (!TestNotNull(TEXT("A creator landscape is placed"), Landscape))
        return false;
    Landscape->SetLandscapeGuid(FGuid::NewGuid());
    Landscape->CreateLandscapeInfo();
    ON_SCOPE_EXIT
    {
        World->EditorDestroyActor(Landscape, false);
    };
    auto* Collision = NewObject<ULandscapeHeightfieldCollisionComponent>(
        Landscape, TEXT("GroundCollision"), RF_Transactional);
    Landscape->AddInstanceComponent(Collision);
    const int32 Parts = Landscape->GetInstanceComponents().Num();
    FP4GroundType Soil;
    Soil.Name = TEXT("Soil");
    Soil.Tag = 0x80400022u;
    FString Error;
    if (!TestTrue(
            TEXT("The ordinary ground action supports a landscape"), P4SetCreatorGroundType(Landscape, Soil, Error)))
    {
        AddError(Error);
        return false;
    }
    auto Fields = P4GetStockEngineFields(Collision);
    if (!TestTrue(TEXT("The landscape's actual collision holds one terrain-tag field"),
            Fields.Num() == 1 && Fields[0].Tags.Num() == 1))
        return false;
    TestEqual(TEXT("The exact selected footstep tag is held"), Fields[0].Tags[0], Soil.Tag);
    Soil.Tag = 0x83c00022u;
    TestTrue(TEXT("A later ground choice updates the same record"), P4SetCreatorGroundType(Landscape, Soil, Error));
    TestEqual(TEXT("Collision is not duplicated"), Landscape->GetInstanceComponents().Num(), Parts);
    TestEqual(TEXT("The changed tag is held"), P4GetStockEngineFields(Collision)[0].Tags[0], Soil.Tag);
    Soil.Tag = 0xc0400022u;
    TestFalse(TEXT("A wall tag is refused for landscape ground"), P4SetCreatorGroundType(Landscape, Soil, Error));
    TestEqual(
        TEXT("A refused choice preserves the previous tag"), P4GetStockEngineFields(Collision)[0].Tags[0], 0x83c00022u);
    return !HasAnyErrors();
}
#endif
