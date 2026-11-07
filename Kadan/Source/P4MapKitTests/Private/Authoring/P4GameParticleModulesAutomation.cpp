#include "P4GameParticleModules.h"
#include "P4OriginalParticlePreflight.h"
#include "P4RecoveredParticleSystem.h"
#include "P4OriginalRecord.h"
#include "AssetRegistryModule.h"
#include "HAL/PlatformMisc.h"
#include "Modules/ModuleManager.h"
#include "Particles/ParticleModule.h"
#include "Particles/Location/ParticleModuleLocationBase.h"
#include "Particles/Camera/ParticleModuleCameraOffset.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionFloatUniform.h"
#include "Distributions/DistributionVectorConstantCurve.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/MetaData.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4GameParticleModuleTests
{
const FStructProperty* TableOf(const UObject* Owner, const TCHAR* Field)
{
    const auto* Raw = FindFProperty<FStructProperty>(Owner->GetClass(), Field);
    return Raw ? FindFProperty<FStructProperty>(Raw->Struct, TEXT("Table")) : nullptr;
}
void* TableValue(UObject* Owner, const TCHAR* Field)
{
    const auto* Raw = FindFProperty<FStructProperty>(Owner->GetClass(), Field);
    return TableOf(Owner, Field)->ContainerPtrToValuePtr<void>(Raw->ContainerPtrToValuePtr<void>(Owner));
}
} //namespace P4GameParticleModuleTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameParticleModuleDeclarationTest, "P4MapKit.Particles.GameModuleDeclarations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameParticleModuleDeclarationTest::RunTest(const FString&)
{
    UClass* Class = P4RegisterGameParticleModuleClasses();
    if (!TestNotNull(TEXT("The game's circle location module class is declared"), Class))
        return false;
    TestTrue(TEXT("It keeps the game's script identity"),
        Class == FindObject<UClass>(nullptr, TEXT("/Script/Engine.ParticleModuleLocationPrimitiveC4Circle")));
    TestTrue(TEXT("A second registration returns the same class"), P4RegisterGameParticleModuleClasses() == Class);
    TestTrue(
        TEXT("It keeps the captured parent"), Class->GetSuperClass() == UParticleModuleLocationBase::StaticClass());
    TestEqual(TEXT("It is marked as a declaration without game behavior"),
        FString(Class->GetOutermost()->GetMetaData()->GetValue(Class, TEXT("P4DeclarationOnly"))), FString(TEXT("1")));
    const TCHAR* Names[] = {TEXT("StartLocation"), TEXT("StartRadius"), TEXT("Division"), TEXT("fSweepStart"),
        TEXT("fSweepEnd"), TEXT("Velocity"), TEXT("VelocityScale"), TEXT("bRandomSpawn"), TEXT("fDivisionRadian"),
        TEXT("fDivisionRandomRadian"), TEXT("fSweepStartRadian")};
    int32 Index = 0;
    bool Ordered = true;
    for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It, ++Index)
        Ordered &= Index < int32(UE_ARRAY_COUNT(Names)) && It->GetName() == Names[Index];
    TestTrue(TEXT("It has the eleven captured fields in the captured order"),
        Ordered && Index == int32(UE_ARRAY_COUNT(Names)));
    TestTrue(TEXT("Distribution fields are the engine's raw distribution structs"),
        CastField<FStructProperty>(Class->FindPropertyByName(TEXT("StartLocation")))->Struct ==
                FRawDistributionVector::StaticStruct() &&
            CastField<FStructProperty>(Class->FindPropertyByName(TEXT("StartRadius")))->Struct ==
                FRawDistributionFloat::StaticStruct() &&
            CastField<FStructProperty>(Class->FindPropertyByName(TEXT("VelocityScale")))->Struct ==
                FRawDistributionFloat::StaticStruct());
    const auto* SweepEnd = CastField<FFloatProperty>(Class->FindPropertyByName(TEXT("fSweepEnd")));
    const auto* Division = CastField<FIntProperty>(Class->FindPropertyByName(TEXT("Division")));
    if (!TestTrue(TEXT("Scalar fields are typed"), SweepEnd && Division))
        return false;
    auto* Defaults = CastChecked<UParticleModule>(Class->GetDefaultObject());
    TestTrue(TEXT("Class defaults are the game's"),
        Defaults->bSpawnModule && Defaults->bSupported3DDrawMode &&
            SweepEnd->GetPropertyValue_InContainer(Defaults) == 360.f &&
            Division->GetPropertyValue_InContainer(Defaults) == 0);
    UClass* Decal = FindObject<UClass>(nullptr, TEXT("/Script/Engine.ParticleModuleTypeDataDecal"));
    if (!TestNotNull(TEXT("The game's decal type data class is declared"), Decal))
        return false;
    auto* DecalDefaults = CastChecked<UParticleModule>(Decal->GetDefaultObject());
    const auto* Fade = CastField<FFloatProperty>(Decal->FindPropertyByName(TEXT("FadeScreenSize")));
    const auto* Color = CastField<FNameProperty>(Decal->FindPropertyByName(TEXT("MaterialColorParamName")));
    TestTrue(TEXT("The decal type data keeps its captured parent and defaults"),
        Decal->GetSuperClass()->GetName() == TEXT("ParticleModuleTypeDataBase") && Fade && Color &&
            DecalDefaults->bSupported3DDrawMode && Fade->GetPropertyValue_InContainer(DecalDefaults) == 0.01f &&
            Color->GetPropertyValue_InContainer(DecalDefaults) == FName(TEXT("DecalColor")));
    TStrongObjectPtr<UParticleSystem> System(P4NewRecoveredParticleSystem());
    auto* Module = Cast<UParticleModule>(NewObject<UObject>(System.Get(), Class));
    if (!TestNotNull(TEXT("An instance is constructed by the stock parent"), Module))
        return false;
    TStrongObjectPtr<UParticleModule> KeepModule(Module);
    TestTrue(TEXT("A new instance has the game's defaults, inherited switches included"),
        Module->bSpawnModule && Module->bSupported3DDrawMode &&
            SweepEnd->GetPropertyValue_InContainer(Module) == 360.f);
    Division->SetPropertyValue_InContainer(Module, 12);
    TestEqual(TEXT("A field holds its own value"), Division->GetPropertyValue_InContainer(Module), 12);
    const auto* Radius = CastField<FStructProperty>(Class->FindPropertyByName(TEXT("StartRadius")));
    auto* Curve = NewObject<UDistributionFloatConstant>(Module);
    Radius->ContainerPtrToValuePtr<FRawDistributionFloat>(Module)->Distribution = Curve;
    TWeakObjectPtr<UDistributionFloatConstant> Held(Curve);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestTrue(TEXT("A curve referenced only through a declared field survives garbage collection"), Held.IsValid());
    UClass* Sphere = FindObject<UClass>(nullptr, TEXT("/Script/Engine.ParticleModuleLocationPrimitiveC4Sphere"));
    if (!TestNotNull(TEXT("The game's sphere storage class is declared"), Sphere))
        return false;
    TestTrue(TEXT("C4Sphere retains LocationBase rather than stock PrimitiveSphere's different schema"),
        Sphere->GetSuperClass() == UParticleModuleLocationBase::StaticClass());
    const TCHAR* SphereNames[] = {TEXT("StartLocation"), TEXT("StartRadius"), TEXT("DivisionType"), TEXT("fSweepStart"),
        TEXT("fSweepEnd"), TEXT("Velocity"), TEXT("VelocityScale"), TEXT("bRandomSpawn"), TEXT("Division"),
        TEXT("bInverseOrder"), TEXT("PolyhedronParamStartIndex"), TEXT("PolyhedronParamNum"), TEXT("fSweepStartDegree"),
        TEXT("fSweepRadianDegree")};
    Index = 0;
    Ordered = true;
    for (TFieldIterator<FProperty> It(Sphere, EFieldIteratorFlags::ExcludeSuper); It; ++It, ++Index)
        Ordered &= Index < int32(UE_ARRAY_COUNT(SphereNames)) && It->GetName() == SphereNames[Index];
    TestTrue(TEXT("C4Sphere has all fourteen captured own fields in order"),
        Ordered && Index == int32(UE_ARRAY_COUNT(SphereNames)));
    const auto* DivisionType = FindFProperty<FEnumProperty>(Sphere, TEXT("DivisionType"));
    UEnum* Enum = DivisionType ? DivisionType->GetEnum() : nullptr;
    const auto* Underlying = DivisionType ? CastField<FByteProperty>(DivisionType->GetUnderlyingProperty()) : nullptr;
    if (!TestTrue(TEXT("DivisionType is a real EnumProperty with byte storage and exact script identity"),
            Enum && Enum->GetPathName() == TEXT("/Script/Engine.EDivisionType") && Underlying && !Underlying->Enum &&
                Underlying->GetSize() == sizeof(uint8) && DivisionType->GetSize() == sizeof(uint8)))
        return false;
    TArray<FName> ExpectedSphereFields;
    for (const TCHAR* Name : SphereNames)
        ExpectedSphereFields.Add(FName(Name));
    TestTrue(TEXT("The actual sphere has a finite complete own field chain and exactly owned enum byte"),
        P4GameParticleModuleFields::ReadyToLink(Sphere, ExpectedSphereFields));
    {
        TStrongObjectPtr<UClass> Unlinked(NewObject<UClass>(GetTransientPackage(), NAME_None, RF_Transient));
        FEnumProperty* EnumField = nullptr;
        for (int32 Item = ExpectedSphereFields.Num() - 1; Item >= 0; --Item)
        {
            FField* Previous = Unlinked->ChildProperties;
            FProperty* Field = nullptr;
            if (Item == 2)
            {
                EnumField = new FEnumProperty(Unlinked.Get(), ExpectedSphereFields[Item], RF_Transient);
                EnumField->SetEnum(Enum);
                EnumField->AddCppProperty(new FByteProperty(EnumField, TEXT("UnderlyingType"), RF_Transient));
                Field = EnumField;
            }
            else
                Field = new FFloatProperty(Unlinked.Get(), ExpectedSphereFields[Item], RF_Transient);
            if (!TestTrue(TEXT("The unlinked constructors never insert themselves into the class"),
                    Unlinked->ChildProperties == Previous && !Field->Next))
                return false;
            Unlinked->AddCppProperty(Field);
        }
        TestTrue(TEXT("Before any StaticLink the full fourteen-field chain and enum child are qualified"),
            P4GameParticleModuleFields::ReadyToLink(Unlinked.Get(), ExpectedSphereFields));
        {
            FField* Next = EnumField->Next;
            ON_SCOPE_EXIT
            {
                EnumField->Next = Next;
            };
            EnumField->Next = EnumField;
            TestFalse(TEXT("The actual startup self-Next state refuses finitely before StaticLink"),
                P4GameParticleModuleFields::ReadyToLink(Unlinked.Get(), ExpectedSphereFields));
        }
        {
            FNumericProperty* Byte = EnumField->GetUnderlyingProperty();
            FField* Next = Byte->Next;
            ON_SCOPE_EXIT
            {
                Byte->Next = Next;
            };
            Byte->Next = EnumField;
            TestFalse(TEXT("The enum has exactly its sole byte child, with no extra field chain"),
                P4GameParticleModuleFields::ReadyToLink(Unlinked.Get(), ExpectedSphereFields));
        }
        {
            FField* First = Unlinked->ChildProperties;
            ON_SCOPE_EXIT
            {
                Unlinked->ChildProperties = First;
            };
            Unlinked->ChildProperties = First->Next;
            TestFalse(TEXT("A missing own field cannot be hidden by a finite shorter chain"),
                P4GameParticleModuleFields::ReadyToLink(Unlinked.Get(), ExpectedSphereFields));
        }
        TestTrue(TEXT("Every temporary bad link is restored before descriptor cleanup"),
            P4GameParticleModuleFields::ReadyToLink(Unlinked.Get(), ExpectedSphereFields));
    }
    TestTrue(TEXT("The storage enum has compatible scoped editor C++ naming"),
        Enum->CppType == TEXT("EDivisionType") && Enum->GetCppForm() == UEnum::ECppForm::EnumClass);
    const TCHAR* EnumNames[] = {TEXT("EDivisionType::None"), TEXT("EDivisionType::DIVISION_8"),
        TEXT("EDivisionType::DIVISION_12"), TEXT("EDivisionType::DIVISION_20"),
        TEXT("EDivisionType::EDivisionType_MAX")};
    const int64 EnumValues[] = {0, 8, 12, 20, 21};
    TestEqual(TEXT("The sparse enum has exactly the captured five entries"), Enum->NumEnums(),
        int32(UE_ARRAY_COUNT(EnumNames)));
    for (int32 Item = 0; Item < int32(UE_ARRAY_COUNT(EnumNames)); ++Item)
    {
        TestEqual(TEXT("The enum keeps each exact scoped name"), Enum->GetNameByIndex(Item).ToString(),
            FString(EnumNames[Item]));
        TestEqual(TEXT("The enum keeps each sparse value"), Enum->GetValueByIndex(Item), EnumValues[Item]);
    }
    auto* SphereDefaults = CastChecked<UParticleModule>(Sphere->GetDefaultObject());
    for (const TCHAR* Name : {TEXT("StartLocation"), TEXT("StartRadius"), TEXT("VelocityScale")})
    {
        const auto* Raw = FindFProperty<FStructProperty>(Sphere, Name);
        const UScriptStruct* ExpectedStruct = FString(Name) == TEXT("StartLocation")
            ? FRawDistributionVector::StaticStruct()
            : FRawDistributionFloat::StaticStruct();
        if (!TestTrue(
                TEXT("Every sphere distribution keeps its exact native struct"), Raw && Raw->Struct == ExpectedStruct))
            return false;
        FStructOnScope Empty(Raw->Struct);
        TestTrue(TEXT("Every sphere distribution default is empty, with no default curve subobject"),
            Raw->Struct->CompareScriptStruct(
                Empty.GetStructMemory(), Raw->ContainerPtrToValuePtr<void>(SphereDefaults), PPF_None));
    }
    for (const TCHAR* Name : {TEXT("fSweepStart"), TEXT("fSweepStartDegree"), TEXT("fSweepRadianDegree")})
    {
        const auto* Value = FindFProperty<FFloatProperty>(Sphere, Name);
        TestTrue(TEXT("The other sphere angle defaults are zero"),
            Value && Value->GetPropertyValue_InContainer(SphereDefaults) == 0.f);
    }
    for (const TCHAR* Name : {TEXT("Division"), TEXT("PolyhedronParamStartIndex"), TEXT("PolyhedronParamNum")})
    {
        const auto* Value = FindFProperty<FIntProperty>(Sphere, Name);
        TestTrue(TEXT("All stored sphere index defaults are zero"),
            Value && Value->GetPropertyValue_InContainer(SphereDefaults) == 0);
    }
    for (const TCHAR* Name : {TEXT("Velocity"), TEXT("bRandomSpawn"), TEXT("bInverseOrder")})
    {
        const auto* Value = FindFProperty<FBoolProperty>(Sphere, Name);
        TestTrue(TEXT("All sphere own switch defaults are false"),
            Value && !Value->GetPropertyValue_InContainer(SphereDefaults));
    }
    TestEqual(TEXT("The sphere class enum default is exactly None"),
        Underlying->GetUnsignedIntPropertyValue(DivisionType->ContainerPtrToValuePtr<void>(SphereDefaults)), uint64(0));
    TStrongObjectPtr<UParticleModule> SphereInstance(Cast<UParticleModule>(NewObject<UObject>(System.Get(), Sphere)));
    if (!TestNotNull(
            TEXT("A real C4Sphere storage instance uses the stock LocationBase constructor"), SphereInstance.Get()))
        return false;
    const auto* SphereEnd = FindFProperty<FFloatProperty>(Sphere, TEXT("fSweepEnd"));
    const auto* SphereDivision = FindFProperty<FIntProperty>(Sphere, TEXT("Division"));
    if (!TestTrue(
            TEXT("Sphere sweep and stored division keep their exact native scalar types"), SphereEnd && SphereDivision))
        return false;
    TestTrue(TEXT("Sphere defaults preserve the game's switches, sweep and separate derived division"),
        SphereDefaults->bSpawnModule && SphereDefaults->bSupported3DDrawMode && SphereInstance->bSpawnModule &&
            SphereInstance->bSupported3DDrawMode && SphereEnd &&
            SphereEnd->GetPropertyValue_InContainer(SphereDefaults) == 360.f &&
            SphereEnd->GetPropertyValue_InContainer(SphereInstance.Get()) == 360.f && SphereDivision &&
            SphereDivision->GetPropertyValue_InContainer(SphereInstance.Get()) == 0 &&
            Underlying->GetUnsignedIntPropertyValue(DivisionType->ContainerPtrToValuePtr<void>(SphereInstance.Get())) ==
                0);
    Underlying->SetIntPropertyValue(DivisionType->ContainerPtrToValuePtr<void>(SphereInstance.Get()), uint64(12));
    SphereDivision->SetPropertyValue_InContainer(SphereInstance.Get(), 8);
    TestTrue(TEXT("Source enum and stored derived division remain distinct authored fields"),
        Underlying->GetUnsignedIntPropertyValue(DivisionType->ContainerPtrToValuePtr<void>(SphereInstance.Get())) ==
                12 &&
            SphereDivision->GetPropertyValue_InContainer(SphereInstance.Get()) == 8);
    const auto* SphereRadius = FindFProperty<FStructProperty>(Sphere, TEXT("StartRadius"));
    if (!TestNotNull(TEXT("Sphere radius retains native RawDistributionFloat storage"), SphereRadius))
        return false;
    auto* SphereCurve = NewObject<UDistributionFloatConstant>(SphereInstance.Get());
    SphereRadius->ContainerPtrToValuePtr<FRawDistributionFloat>(SphereInstance.Get())->Distribution = SphereCurve;
    TWeakObjectPtr<UDistributionFloatConstant> HeldSphereCurve(SphereCurve);
    TWeakObjectPtr<UEnum> HeldEnum(Enum);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestTrue(TEXT("Sphere native distribution references and enum survive GC"),
        HeldSphereCurve.IsValid() && HeldEnum.IsValid());
    TestTrue(TEXT("Registration retains both location identities"),
        P4RegisterGameParticleModuleClasses() == Class &&
            FindObject<UClass>(nullptr, TEXT("/Script/Engine.ParticleModuleLocationPrimitiveC4Sphere")) == Sphere);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ParticleCurvesFromTablesTest, "P4MapKit.Particles.CurvesFromBakedTables",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ParticleCurvesFromTablesTest::RunTest(const FString&)
{
    using namespace P4GameParticleModuleTests;
    TStrongObjectPtr<UParticleSystem> System(P4NewRecoveredParticleSystem());
    auto* Color = NewObject<UParticleModuleColorOverLife>(System.Get());
    auto* Authored = NewObject<UDistributionVectorConstantCurve>(Color);
    for (const auto& Key :
        {TPair<float, FVector>(0.f, FVector(1.f, 0.f, 0.f)), TPair<float, FVector>(.3f, FVector(.2f, .9f, .1f)),
            TPair<float, FVector>(.7f, FVector(.5f, .4f, 1.f)), TPair<float, FVector>(1.f, FVector(0.f, 0.f, .25f))})
        Authored->ConstantCurve.Points[Authored->ConstantCurve.AddPoint(Key.Key, Key.Value)].InterpMode = CIM_Linear;
    auto* Range = NewObject<UDistributionFloatUniform>(Color);
    Range->Min = .2f;
    Range->Max = .8f;
    Color->ColorOverLife.Distribution = Authored;
    Color->AlphaOverLife.Distribution = Range;
    Authored->bIsDirty = true;
    Range->bIsDirty = true;
    // The stock editor bake, then the cook's shape: tables kept, curve objects gone.
    Color->ColorOverLife.Initialize();
    Color->AlphaOverLife.Initialize();
    const auto* ColorTable = TableOf(Color, TEXT("ColorOverLife"));
    const auto* AlphaTable = TableOf(Color, TEXT("AlphaOverLife"));
    if (!TestTrue(TEXT("The raw distribution tables are reflected"), ColorTable && AlphaTable))
        return false;
    FStructOnScope ColorBefore(ColorTable->Struct), AlphaBefore(AlphaTable->Struct);
    ColorTable->Struct->CopyScriptStruct(ColorBefore.GetStructMemory(), TableValue(Color, TEXT("ColorOverLife")));
    AlphaTable->Struct->CopyScriptStruct(AlphaBefore.GetStructMemory(), TableValue(Color, TEXT("AlphaOverLife")));
    TArray<FVector> Expected;
    for (int32 Step = 0; Step <= 8; ++Step)
        Expected.Add(Color->ColorOverLife.GetValue(Step / 8.f));
    Color->ColorOverLife.Distribution = nullptr;
    Color->AlphaOverLife.Distribution = nullptr;
    int32 Made = 0;
    FString Error;
    TArray<FP4ParticleRawDistributionPlace> Places;
    if (!TestTrue(TEXT("Stripped curves are rebuilt from their tables: ") + Error,
            P4ReconstructOriginalParticleDistributions(Color, Made, Error, &Places)))
        return false;
    TestEqual(TEXT("Both stripped curves are rebuilt"), Made, 2);
    TestEqual(TEXT("Each rebuilt curve has its place"), Places.Num(), 2);
    UDistributionVector* Vector = Color->ColorOverLife.Distribution;
    UDistributionFloat* Float = Color->AlphaOverLife.Distribution;
    if (!TestTrue(TEXT("Both curves are real editor distributions in the module"),
            Vector && Float && Vector->GetOuter() == Color && Float->GetOuter() == Color))
        return false;
    TestTrue(TEXT("A random range comes back as a uniform distribution"),
        Float->IsA<UDistributionFloatUniform>() && CastChecked<UDistributionFloatUniform>(Float)->Min == .2f &&
            CastChecked<UDistributionFloatUniform>(Float)->Max == .8f);
    TestTrue(TEXT("The tables are the baked ones, unchanged"),
        ColorTable->Struct->CompareScriptStruct(
            ColorBefore.GetStructMemory(), TableValue(Color, TEXT("ColorOverLife")), PPF_None) &&
            AlphaTable->Struct->CompareScriptStruct(
                AlphaBefore.GetStructMemory(), TableValue(Color, TEXT("AlphaOverLife")), PPF_None));
    TestTrue(
        TEXT("The rebuilt curves are not dirty, so the tables are not rebaked"), !Vector->bIsDirty && !Float->bIsDirty);
    TestTrue(TEXT("The cook drops the rebuilt curves again because their tables are baked"),
        !Vector->NeedsLoadForClient() && !Vector->NeedsLoadForServer() && !Float->NeedsLoadForClient() &&
            !Float->NeedsLoadForServer());
    bool Close = true;
    for (int32 Step = 0; Step <= 8; ++Step)
        Close &= Vector->GetValue(Step / 8.f).Equals(Expected[Step], 1.e-5f) &&
            Color->ColorOverLife.GetValue(Step / 8.f).Equals(Expected[Step], 0.f);
    TestTrue(TEXT("The rebuilt curve gives the table's values and the table still gives its own"), Close);
    TestTrue(TEXT("A second pass keeps the rebuilt curves"),
        P4ReconstructOriginalParticleDistributions(Color, Made, Error) && Made == 0 &&
            Color->ColorOverLife.Distribution == Vector && Color->AlphaOverLife.Distribution == Float);
    // A table no stock distribution makes is refused and left as it is.
    auto* Camera = NewObject<UParticleModuleCameraOffset>(System.Get());
    auto* Offset = NewObject<UDistributionFloatConstant>(Camera);
    Offset->Constant = 3.f;
    Offset->bIsDirty = true;
    Camera->CameraOffset.Distribution = Offset;
    Camera->CameraOffset.Initialize();
    Camera->CameraOffset.Distribution = nullptr;
    const auto* Stride = FindFProperty<FByteProperty>(
        TableOf(Camera, TEXT("CameraOffset"))->Struct, TEXT("EntryStride"));
    if (!TestNotNull(TEXT("The table stride is reflected"), Stride))
        return false;
    Stride->SetPropertyValue_InContainer(TableValue(Camera, TEXT("CameraOffset")), 3);
    TestFalse(TEXT("An impossible table is refused"), P4ReconstructOriginalParticleDistributions(Camera, Made, Error));
    TestTrue(TEXT("The refusal names the place and the reason"),
        Error.StartsWith(TEXT("particle-native-distribution-table-unproducible:")) &&
            Error.Contains(TEXT("CameraOffset")));
    TestNull(TEXT("A refused table gets no curve"), Camera->CameraOffset.Distribution);
    return !HasAnyErrors();
}
// Opt-in, in a fresh editor after an import: every original particle system of the project
// loads cold through the ordinary engine path and its native cook equals its import baseline.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ProjectOriginalParticlesColdTest, "P4MapKit.Particles.ProjectOriginalsCold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ProjectOriginalParticlesColdTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARTICLE_COLD_PROJECT")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "ProjectOriginalsCold not exercised: set P4_NATIVE_PARTICLE_COLD_PROJECT=1 in a fresh editor after an import."));
        return true;
    }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FAssetData> Assets;
    Registry.GetAssetsByPath(TEXT("/Game"), Assets, true);
    int32 Loaded = 0, Compared = 0, ReferenceOnly = 0;
    for (const FAssetData& Asset : Assets)
    {
        if (Asset.AssetClass != TEXT("ParticleSystem") && Asset.AssetClass != TEXT("P4RecoveredParticleSystem"))
            continue;
        auto* System = Cast<UParticleSystem>(Asset.GetAsset());
        if (!TestNotNull(TEXT("Original particle system loads: ") + Asset.ObjectPath.ToString(), System))
            continue;
        ++Loaded;
        const auto Status = P4QueryOriginalAsset(System);
        if (!Status.IsOriginal())
            continue;
        if (Status.Record.ReferenceOnly)
        {
            ++ReferenceOnly;
            continue;
        }
        const auto Diagnostic = P4DiagnoseOriginalAsset(System);
        ++Compared;
        if (!Diagnostic.Error.IsEmpty() || Diagnostic.Comparison != EP4OriginalComparison::Equal)
            AddError(Asset.PackageName.ToString() + TEXT(" differs from its import baseline after a cold load. ") +
                Diagnostic.Error);
    }
    AddInfo(FString::Printf(
        TEXT("Particle systems loaded %d, compared %d, reference only %d."), Loaded, Compared, ReferenceOnly));
    TestTrue(TEXT("At least one original particle system was compared"), Compared > 0);
    return !HasAnyErrors();
}
#endif
