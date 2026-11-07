#include "P4RecoveredParticleSystem.h"
#include "P4KadanImportSources.h"
#include "P4ParticleDistributionDefaults.h"
#include "P4OriginalParticlePreflight.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSpriteEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/Camera/ParticleModuleCameraOffset.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Particles/Color/ParticleModuleColorScaleOverLife.h"
#include "Particles/TypeData/ParticleModuleTypeDataGpu.h"
#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionVectorConstant.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4OriginalParticleTests
{
void Put(TArray<uint8>& Bytes, int32 Offset, uint32 Value)
{
    for (int32 I = 0; I < 4; ++I)
        Bytes[Offset + I] = uint8(Value >> (8 * I));
}
TArray<uint8> Tail(const TArray<FVector2D>& Geometry)
{
    TArray<uint8> Bytes;
    Bytes.SetNumZeroed(12 + Geometry.Num() * 8);
    Put(Bytes, 4, 1);
    Put(Bytes, 8, Geometry.Num());
    if (Geometry.Num())
        FMemory::Memcpy(Bytes.GetData() + 12, Geometry.GetData(), Geometry.Num() * 8);
    return Bytes;
}
} //namespace P4OriginalParticleTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalParticleStorageTest, "P4MapKit.Particles.NativeStorageAndRequiredTail",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalParticleStorageTest::RunTest(const FString&)
{
    UClass* Parent = UParticleSystem::StaticClass();
    UClass* Recovered = P4RecoveredParticleSystemClass();
    if (!TestNotNull(TEXT("Original game particle schema is registered separately"), Recovered))
        return false;
    TestEqual(TEXT("Recovered particle retains its actual runtime parent's namespace"), Recovered->GetOutermost(),
        Parent->GetOutermost());
    TestTrue(TEXT("Original native constructor and reference collection are retained"),
        Recovered->ClassConstructor == Parent->ClassConstructor &&
            Recovered->ClassAddReferencedObjects == Parent->ClassAddReferencedObjects);
    UParticleSystem* System = P4NewRecoveredParticleSystem();
    if (!TestNotNull(TEXT("Actual ParticleSystem native object is constructed"), System))
        return false;
    for (const TCHAR* Name : {TEXT("bLinkedToActorMeshCulling"), TEXT("bCheckInfiniteLifetime"),
             TEXT("bInfiniteLifetime"), TEXT("bTickGroupOverwrite")})
    {
        const auto* Field = FindFProperty<FBoolProperty>(Recovered, Name);
        if (!TestTrue(TEXT("Every original field has its own real native bool storage"),
                Field && Field->IsNativeBool() && Field->GetOwnerStruct() == Recovered &&
                    Field->GetOffset_ForInternal() >= Parent->GetPropertiesSize()))
            return false;
        TestNull(TEXT("The stock engine class was not modified"), FindFProperty<FBoolProperty>(Parent, Name));
        const bool OriginalDefault = FString(Name) == TEXT("bLinkedToActorMeshCulling");
        TestEqual(TEXT("New original particle inherits the captured game native CDO bool value"),
            Field->GetPropertyValue_InContainer(System), OriginalDefault);
        TestEqual(TEXT("Recovered serialization baseline holds the captured game native CDO bool value"),
            Field->GetPropertyValue_InContainer(Recovered->GetDefaultObject()), OriginalDefault);
        Field->SetPropertyValue_InContainer(System, true);
        TestTrue(TEXT("An original bool delta is held as an actual typed value"),
            Field->GetPropertyValue_InContainer(System));
    }
    auto* Module = NewObject<UParticleModuleRequired>(System, TEXT("OriginalRequired"));
    const FString SourceRequired = System->GetName() + TEXT(".OriginalRequired");
    TestTrue(TEXT("Cooked dot-separated export identity resolves the exact live colon-separated subobject"),
        Module->GetPathName().Contains(TEXT(":")) && P4FindOriginalParticleObject(System, SourceRequired) == Module);
    auto* NestedOwner = NewObject<UTexture2D>(System, TEXT("NestedOwner"));
    auto* NestedModule = NewObject<UParticleModuleRequired>(NestedOwner, TEXT("OriginalRequired"));
    TestTrue(TEXT("Identical leaf names resolve only through their full source outer chain"),
        P4FindOriginalParticleObject(System, System->GetName() + TEXT(".NestedOwner.OriginalRequired")) ==
                NestedModule &&
            P4FindOriginalParticleObject(System, SourceRequired) == Module);
    TestNull(TEXT("A different source asset root is refused"),
        P4FindOriginalParticleObject(System, TEXT("DifferentAsset.OriginalRequired")));
    TestNull(TEXT("A missing source owner cannot fall back to a matching leaf"),
        P4FindOriginalParticleObject(System, System->GetName() + TEXT(".MissingOwner.OriginalRequired")));
    TestNull(TEXT("A differently cased source name is refused despite FName case folding"),
        P4FindOriginalParticleObject(System, System->GetName() + TEXT(".originalrequired")));
    TestNull(TEXT("Empty source owner segments are refused"),
        P4FindOriginalParticleObject(System, System->GetName() + TEXT("..OriginalRequired")));
    TestNull(TEXT("A live colon path is not accepted as a source export identity"),
        P4FindOriginalParticleObject(System, System->GetName() + TEXT(":OriginalRequired")));
    auto* OtherSystem = P4NewRecoveredParticleSystem();
    TestNull(TEXT("Another particle system cannot lend an identically named source object"),
        P4FindOriginalParticleObject(OtherSystem, SourceRequired));
    OtherSystem->MarkPendingKill();
    auto* Texture = NewObject<UTexture2D>(System);
    auto* Camera = NewObject<UParticleModuleCameraOffset>(System);
    const FProperty* CameraField = Camera->GetClass()->FindPropertyByName(TEXT("CameraOffset"));
    using V = P4MapKit::FDecodedValue;
    V Raw, Table, Lookup;
    Lookup.Kind = V::EKind::Array;
    Lookup.Items.push_back(V::Atom(V::EKind::Number, "5.0000004768371582"));
    Table.Fields.emplace_back("Values", Lookup);
    Raw.Fields.emplace_back("MinValue", V::Atom(V::EKind::Number, "5"));
    Raw.Fields.emplace_back("Distribution", V::Atom(V::EKind::Null, ""));
    Raw.Fields.emplace_back("Table", Table);
    V ExpectedRaw = Raw;
    ExpectedRaw.Fields.erase(ExpectedRaw.Fields.begin() + 1);
    V CanonicalRaw = Raw;
    TestTrue(TEXT("Only a proven native raw-distribution null default canonicalizes its redundant null tag"),
        P4CanonicalOriginalParticleDistribution(CanonicalRaw, CameraField) &&
            P4MapKit::DecodedJson(CanonicalRaw) == P4MapKit::DecodedJson(ExpectedRaw));
    auto* Color = NewObject<UParticleModuleColorOverLife>(System);
    V VectorRaw = Raw;
    TestTrue(TEXT("The separately proven native vector-distribution null default has the same bounded rule"),
        P4CanonicalOriginalParticleDistribution(
            VectorRaw, Color->GetClass()->FindPropertyByName(TEXT("ColorOverLife"))) &&
            P4MapKit::DecodedJson(VectorRaw) == P4MapKit::DecodedJson(ExpectedRaw));
    V WrongType = Raw;
    TestFalse(TEXT("An unrelated native property's null member is not normalized"),
        P4CanonicalOriginalParticleDistribution(
            WrongType, Module->GetClass()->FindPropertyByName(TEXT("AlphaThreshold"))));
    TestTrue(TEXT("Wrong-type refusal preserves every source field"),
        P4MapKit::DecodedJson(WrongType) == P4MapKit::DecodedJson(Raw));
    V NonNull = Raw;
    NonNull.Fields[1].second = V::Atom(V::EKind::String, "OriginalDistribution");
    const std::string NonNullBefore = P4MapKit::DecodedJson(NonNull);
    TestFalse(TEXT("An authored non-null distribution reference remains comparable"),
        P4CanonicalOriginalParticleDistribution(NonNull, CameraField));
    TestTrue(TEXT("Non-null refusal preserves reference and lookup values"),
        P4MapKit::DecodedJson(NonNull) == NonNullBefore);
    V SourceCamera;
    SourceCamera.Fields.emplace_back("CameraOffset", Raw);
    int32 NullsChecked = 0;
    FString DistributionError;
    Camera->CameraOffset.Distribution = nullptr;
    TestTrue(TEXT("An actual source-null distribution is held as null"),
        P4OriginalParticleDistributionNullsHeld(Camera, SourceCamera, NullsChecked, DistributionError) &&
            NullsChecked == 1);
    Camera->CameraOffset.Distribution = NewObject<UDistributionFloatConstant>(Camera);
    TestFalse(TEXT("A native non-null distribution cannot pass the independent source-null proof"),
        P4OriginalParticleDistributionNullsHeld(Camera, SourceCamera, NullsChecked, DistributionError));
    Camera->CameraOffset.Distribution = nullptr;
    // First-party source pixels exercise the installed engine's actual cutout build/Serialize.
    TArray<uint8> Pixels;
    Pixels.SetNumZeroed(16 * 16 * 4);
    for (int32 Y = 3; Y < 13; ++Y)
        for (int32 X = 4; X < 12; ++X)
            for (int32 Channel = 0; Channel < 4; ++Channel)
                Pixels[(Y * 16 + X) * 4 + Channel] = 255;
    Texture->Source.Init(16, 16, 1, 1, TSF_BGRA8, Pixels.GetData());
    Module->CutoutTexture = Texture;
    Module->SubImages_Horizontal = Module->SubImages_Vertical = 1;
    Module->BoundingMode = BVC_EightVertices;
    Module->OpacitySourceMode = OSM_Alpha;
    Module->AlphaThreshold = .05f;
    FSubUVDerivedData Expected;
    Expected.Build(Texture, 1, 1, BVC_EightVertices, .05f, OSM_Alpha);
    const TArray<uint8> SourceTail = P4OriginalParticleTests::Tail(Expected.BoundingGeometry);
    FString Error;
    if (!TestTrue(TEXT("Bounded original tail is loaded by the stock public native serializer: ") + Error,
            P4ApplyOriginalParticleRequiredTail(Module, SourceTail, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("This separate first-party pixel fixture really regenerates its geometry"),
        P4ParticleRequiredTailRegeneratesFromTextureSource(Module, SourceTail, Error));
    TestTrue(TEXT("Exact native coordinate bits are held before PostLoad"),
        P4OriginalParticleRequiredTailMatches(Module, SourceTail, Error));
    TArray<FString> BeforeLines, AfterLines;
    TestTrue(TEXT("Complete native comparison lines cover every coordinate word"),
        P4DescribeOriginalParticleRequiredTail(SourceTail, BeforeLines, Error) &&
            BeforeLines.Num() == Expected.BoundingGeometry.Num() + 1);
    static_cast<UObject*>(Module)->PostLoad();
    TestTrue(TEXT("Ordinary stock PostLoad derives the same bits and initializes its own render buffer"),
        P4OriginalParticleRequiredTailMatches(Module, SourceTail, Error));
    TArray<uint8> Different = SourceTail;
    if (Different.Num() > 12)
        Different[12] ^= 1;
    TestTrue(TEXT("A single finite coordinate-bit edit changes full native comparison lines"),
        P4DescribeOriginalParticleRequiredTail(Different, AfterLines, Error) && BeforeLines != AfterLines);
    TestFalse(TEXT("Geometry that actual source pixels do not generate is refused"),
        P4ApplyOriginalParticleRequiredTail(Module, Different, Error));
    TestFalse(TEXT("The distinct pixel-regeneration proof refuses changed geometry"),
        P4ParticleRequiredTailRegeneratesFromTextureSource(Module, Different, Error));
    FP4KadanSourceBinding NoSource;
    TestFalse(TEXT("Native-only arbitrary geometry cannot authorize original-source cache publication"),
        P4CaptureOriginalParticleCutouts(System, NoSource, Error));
    TestTrue(TEXT("Refusal preserves previously held geometry"),
        P4OriginalParticleRequiredTailMatches(Module, SourceTail, Error));
    Module->CutoutTexture = nullptr;
    TestFalse(TEXT("Missing actual cutout texture is refused"),
        P4ApplyOriginalParticleRequiredTail(Module, SourceTail, Error));
    Module->CutoutTexture = Texture;
    TestTrue(TEXT("The actual cutout texture remains a typed resource"), Module->CutoutTexture == Texture);
    auto* Emitter = NewObject<UParticleSpriteEmitter>(System);
    auto* Lod = NewObject<UParticleLODLevel>(Emitter);
    Emitter->LODLevels.Add(Lod);
    System->Emitters.Add(Emitter);
    Lod->RequiredModule = Module;
    Lod->SpawnModule = NewObject<UParticleModuleSpawn>(System);
    Lod->Modules.Add(Color);
    Color->ColorOverLife.Distribution = nullptr;
    Color->AlphaOverLife.Distribution = nullptr;
    TestTrue(TEXT("The exact cooked CPU null-distribution shape remains eligible"),
        P4PreflightOriginalParticleEditorBuild(System, Error));
    Lod->TypeDataModule = NewObject<UParticleModuleTypeDataGpu>(System);
    TestFalse(TEXT("The evidenced GPU color-null editor dereference is refused before native rebuild"),
        P4PreflightOriginalParticleEditorBuild(System, Error));
    Color->ColorOverLife.Distribution = NewObject<UDistributionVectorConstant>(Color);
    TestFalse(TEXT("The evidenced GPU alpha-null editor dereference is independently refused"),
        P4PreflightOriginalParticleEditorBuild(System, Error));
    Color->AlphaOverLife.Distribution = NewObject<UDistributionFloatConstant>(Color);
    TestTrue(TEXT("A GPU with the required real editor curve objects is not blanket blocked"),
        P4PreflightOriginalParticleEditorBuild(System, Error));
    auto* ColorScale = NewObject<UParticleModuleColorScaleOverLife>(System);
    Lod->Modules.Add(ColorScale);
    ColorScale->ColorScaleOverLife.Distribution = nullptr;
    ColorScale->AlphaScaleOverLife.Distribution = nullptr;
    TestFalse(TEXT("The exact GPU color-scale null dereference is refused"),
        P4PreflightOriginalParticleEditorBuild(System, Error));
    ColorScale->ColorScaleOverLife.Distribution = NewObject<UDistributionVectorConstant>(ColorScale);
    TestFalse(TEXT("The exact GPU alpha-scale null dereference is refused"),
        P4PreflightOriginalParticleEditorBuild(System, Error));
    ColorScale->AlphaScaleOverLife.Distribution = NewObject<UDistributionFloatConstant>(ColorScale);
    TestTrue(TEXT("A complete native GPU curve shape passes preflight without any data changes"),
        P4PreflightOriginalParticleEditorBuild(System, Error));
    System->MarkPendingKill();
    return !HasAnyErrors();
}
#endif
