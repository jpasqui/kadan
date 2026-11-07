#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialCollectionSource.h"
#include "P4KadanTestSeams.h"
#include "P4KadanExecutionEdges.h"
#include "P4KadanImport.h"
#include "P4MaterialGraph.h"
#include "P4NativeHash.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ObjectWriter.h"
#include "ShaderParameterMetadata.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace
{
TArray<uint8> CollectionControlBytes(UObject* Object)
{
    TArray<uint8> Out;
    FObjectWriter Writer(Object, Out, false, false, false);
    return Out;
}
bool CollectionControlMetadataEqual(const TMap<FName, FString>& Before, UObject* Object)
{
    const auto* After = UMetaData::GetMapForObject(Object);
    if (!After || After->Num() != Before.Num())
        return false;
    for (const auto& Row : Before)
    {
        const auto* Value = After->Find(Row.Key);
        if (!Value || !P4ExactNativeString(*Value, Row.Value))
            return false;
    }
    return true;
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialCollectionSourceTest, "P4MapKit.Kadan.MaterialCollectionSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialCollectionSourceTest::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_COLLECTION_SOURCE")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt in with one exact already imported MPC H+B pair; no acquisition, import or saves"));
        return true;
    }
    const FString Package = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_COLLECTION_PACKAGE"));
    auto Row = MakeShared<FP4KadanSourceAsset>();
    Row->Package = Package;
    Row->Kind = TEXT("MaterialParameterCollection");
    Row->Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_COLLECTION_HEADER"));
    Row->Body = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_COLLECTION_BODY"));
    FP4KadanImportSourceScope Scope({Row});
    FP4KadanSourceBinding Binding;
    FString Error;
    if (!TestTrue(
            TEXT("Exact registered canonical source H+B is current"), P4KadanReadBoundSource(Package, Binding, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString Object = Package + TEXT(".") + FPackageName::GetShortName(Package);
    TStrongObjectPtr<UMaterialParameterCollection> Asset(LoadObject<UMaterialParameterCollection>(nullptr, *Object));
    if (!TestNotNull(TEXT("Already genuinely imported native collection"), Asset.Get()))
        return false;
    const auto Before = CollectionControlBytes(Asset.Get());
    const auto Flags = Asset->GetFlags();
    const bool Dirty = Asset->GetOutermost()->IsDirty();
    TMap<FName, FString> Metadata;
    if (const auto* Map = UMetaData::GetMapForObject(Asset.Get()))
        Metadata = *Map;
    const auto Preserved = [&]()
    {
        return Before == CollectionControlBytes(Asset.Get()) && Asset->GetFlags() == Flags &&
            Dirty == Asset->GetOutermost()->IsDirty() && CollectionControlMetadataEqual(Metadata, Asset.Get()) &&
            P4KadanSourceBindingCurrent(Binding, Error);
    };
    ON_SCOPE_EXIT
    {
        TestTrue(TEXT("Actual collection/source/records preserved on every exit"), Preserved());
    };
    if (!TestTrue(TEXT("Actual source contains scalar and vector control parameters"),
            Asset->ScalarParameters.Num() > 0 && Asset->VectorParameters.Num() > 0))
        return false;
    FP4MaterialUsedResourceRequest Request;
    Request.Kind = EP4MaterialUsedResourceKind::Collection;
    Request.Package = Package;
    Request.Object = Object;
    Request.NativeClass = TEXT("/Script/Engine.MaterialParameterCollection");
    Request.SourceSha256 = Binding.SourceDigest;
    Request.bSourceAvailable = true;
    // Controlled consumer descriptors test native binding, not an invented game
    // shader proof. Production consumers come from all-owner shader discovery.
    FP4MaterialResourceOwnerUse Owner;
    Owner.Owner = TEXT("/Game/__P4CollectionBindingControl");
    const FGuid State = Asset->StateId;
    const uint32 Words[] = {State.A, State.B, State.C, State.D};
    FString StateHex;
    for (uint32 Word : Words)
        for (uint32 Byte = 0; Byte < 4; ++Byte)
            StateHex += FString::Printf(TEXT("%02x"), (Word >> (Byte * 8)) & 255u);
    for (const FName Name : {Asset->ScalarParameters[0].ParameterName, Asset->VectorParameters[0].ParameterName})
    {
        FP4MaterialResourceConsumer Use;
        Use.NodeType = TEXT("CollectionParameter");
        Use.Node = Name.ToString();
        Use.Parameter = Name.ToString();
        Use.CollectionUniforms.Add(0);
        Use.CollectionStateIds.Add(StateHex);
        Owner.Consumers.Add(Use);
    }
    Request.Owners.Add(Owner);
    if (!TestTrue(TEXT("Genuine zero-held native import matches all typed source values and bindings"),
            P4OriginalMaterialCollectionMatchesSource(Asset.Get(), Request, Binding, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Genuine cold asset completed actual stock PostLoad before public layout read"),
        Asset->HasAnyFlags(RF_WasLoaded) && P4MaterialCollectionLifecycleControl(Asset.Get(), Error));
    TestTrue(TEXT("Actual native uniform struct matches source packing"),
        P4MaterialCollectionLayoutControl(
            Asset->GetUniformBufferStruct(), Asset->ScalarParameters.Num(), Asset->VectorParameters.Num(), Error));
    const uint32 VectorCount = uint32((Asset->ScalarParameters.Num() + 3) / 4 + Asset->VectorParameters.Num());
    const auto Shape = [&](uint32 Count, EUniformBufferBaseType Type, EShaderPrecisionModifier::Type Precision,
                           uint32 Offset, uint32 Rows, uint32 Columns)
    {
        TArray<FShaderParametersMetadata::FMember> Members;
        Members.Emplace(TEXT("Vectors"), TEXT(""), Offset, Type, Precision, Rows, Columns, Count, nullptr);
        return MakeUnique<FShaderParametersMetadata>(FShaderParametersMetadata::EUseCase::DataDrivenUniformBuffer,
            TEXT("Control"), TEXT("MaterialCollection"), TEXT("MaterialCollection"), nullptr,
            Align(Offset + Count * Rows * Columns * 4u, uint32(SHADER_PARAMETER_STRUCT_ALIGNMENT)), Members);
    };
    const auto RefuseShape = [&](const TCHAR* Label, uint32 Count, EUniformBufferBaseType Type,
                                 EShaderPrecisionModifier::Type Precision, uint32 Offset, uint32 Rows, uint32 Columns)
    {
        const auto M = Shape(Count, Type, Precision, Offset, Rows, Columns);
        TestFalse(Label,
            P4MaterialCollectionLayoutControl(*M, Asset->ScalarParameters.Num(), Asset->VectorParameters.Num(), Error));
    };
    const auto GoodShape = Shape(VectorCount, UBMT_FLOAT32, EShaderPrecisionModifier::Half, 0, 1, 4);
    TestTrue(TEXT("Independent exact stock layout control qualifies"),
        P4MaterialCollectionLayoutControl(
            *GoodShape, Asset->ScalarParameters.Num(), Asset->VectorParameters.Num(), Error));
    RefuseShape(TEXT("Stale native vector count refuses"), VectorCount + 1, UBMT_FLOAT32,
        EShaderPrecisionModifier::Half, 0, 1, 4);
    RefuseShape(
        TEXT("Changed native element type refuses"), VectorCount, UBMT_INT32, EShaderPrecisionModifier::Half, 0, 1, 4);
    RefuseShape(
        TEXT("Changed native precision refuses"), VectorCount, UBMT_FLOAT32, EShaderPrecisionModifier::Float, 0, 1, 4);
    RefuseShape(TEXT("Changed native member offset refuses"), VectorCount, UBMT_FLOAT32, EShaderPrecisionModifier::Half,
        16, 1, 4);
    RefuseShape(TEXT("Changed native vector shape refuses"), VectorCount, UBMT_FLOAT32, EShaderPrecisionModifier::Half,
        0, 2, 4);
    TestTrue(TEXT("Execution bridge reuses the unchanged genuine collection without importing/cooking"),
        P4KadanMaterialResourceReady(Request, *Row, Error));
    bool Needs = true;
    TestTrue(TEXT("Occupied genuine collection is reused, never overwritten"),
        P4KadanMaterialResourceNeedsImport(Request, *Row, Needs, Error) && !Needs);
    TStrongObjectPtr<UMaterialParameterCollection> Copy(NewObject<UMaterialParameterCollection>(GetTransientPackage()));
    const auto Reset = [&]()
    {
        Copy->StateId = Asset->StateId;
        Copy->ScalarParameters = Asset->ScalarParameters;
        Copy->VectorParameters = Asset->VectorParameters;
    };
    Reset();
    TestFalse(TEXT("Unpostloaded native object refuses WITHOUT calling checked private-layout getter"),
        P4MaterialCollectionLifecycleControl(Copy.Get(), Error));
    TestTrue(TEXT("Exact transient copied values qualify only the typed value control"),
        P4MaterialCollectionValueControl(Copy.Get(), Request, Binding, Error));
    TestFalse(TEXT("Transient controls cannot manufacture a genuine source import record"),
        P4OriginalMaterialCollectionMatchesSource(Copy.Get(), Request, Binding, Error));
    const auto Refuses = [&](const TCHAR* Label, const TFunction<void()>& Mutate)
    {
        Reset();
        Mutate();
        TestFalse(Label, P4MaterialCollectionValueControl(Copy.Get(), Request, Binding, Error));
        Reset();
    };
    Refuses(TEXT("Changed source StateId refuses"), [&]() { ++Copy->StateId.A; });
    Refuses(TEXT("Changed scalar parameter GUID refuses"), [&]() { ++Copy->ScalarParameters[0].Id.A; });
    Refuses(TEXT("Changed vector parameter GUID refuses"), [&]() { ++Copy->VectorParameters[0].Id.A; });
    Refuses(TEXT("Changed parameter name refuses"),
        [&]() { Copy->ScalarParameters[0].ParameterName = FName(TEXT("ChangedParameter")); });
    Refuses(TEXT("Scalar one-ULP edit refuses despite a clean package flag"),
        [&]()
        {
            uint32 Bits = 0;
            auto& Value = Copy->ScalarParameters[0].DefaultValue;
            FMemory::Memcpy(&Bits, &Value, 4);
            ++Bits;
            FMemory::Memcpy(&Value, &Bits, 4);
        });
    Refuses(TEXT("Vector one-ULP edit refuses"),
        [&]()
        {
            uint32 Bits = 0;
            auto& Value = Copy->VectorParameters[0].DefaultValue.R;
            FMemory::Memcpy(&Bits, &Value, 4);
            ++Bits;
            FMemory::Memcpy(&Value, &Bits, 4);
        });
    Refuses(TEXT("Duplicate scalar parameter GUID refuses"),
        [&]()
        {
            const auto Duplicate = Copy->ScalarParameters[0];
            Copy->ScalarParameters.Add(Duplicate);
        });
    Refuses(TEXT("Missing vector default refuses"), [&]() { Copy->VectorParameters.RemoveAt(0); });
    for (int32 I = 0; I < Copy->ScalarParameters.Num(); ++I)
        if (Copy->ScalarParameters[I].DefaultValue == 0.0f)
        {
            Refuses(TEXT("Signed zero remains bit-exact"),
                [&]()
                {
                    uint32 Bits = 0;
                    auto& Value = Copy->ScalarParameters[I].DefaultValue;
                    FMemory::Memcpy(&Bits, &Value, 4);
                    Bits ^= 0x80000000u;
                    FMemory::Memcpy(&Value, &Bits, 4);
                });
            break;
        }
    if (Copy->ScalarParameters.Num() > 1)
        Refuses(TEXT("Scalar source order is immutable"), [&]() { Copy->ScalarParameters.Swap(0, 1); });
    if (Copy->VectorParameters.Num() > 1)
        Refuses(TEXT("Vector source order is immutable"), [&]() { Copy->VectorParameters.Swap(0, 1); });
    auto Wrong = Request;
    Wrong.Owners[0].Consumers[0].CollectionStateIds[0][0] = StateHex[0] == TCHAR('0') ? TCHAR('1') : TCHAR('0');
    TestFalse(TEXT("Shader binding to another StateId refuses"),
        P4MaterialCollectionValueControl(Copy.Get(), Wrong, Binding, Error));
    Wrong = Request;
    Wrong.Owners[0].Consumers[0].Parameter = TEXT("MissingParameter");
    TestFalse(TEXT("Missing shader-used parameter refuses"),
        P4MaterialCollectionValueControl(Copy.Get(), Wrong, Binding, Error));
    Wrong = Request;
    Wrong.Owners[0].Consumers[0].CollectionStateIds.Reset();
    TestFalse(TEXT("Incomplete uniform/state binding refuses"),
        P4MaterialCollectionValueControl(Copy.Get(), Wrong, Binding, Error));
    auto ChangedBinding = Binding;
    ChangedBinding.Body.Last() ^= 1;
    TestFalse(TEXT("Changed held source buffer cannot reuse its old digest"),
        P4MaterialCollectionValueControl(Copy.Get(), Request, ChangedBinding, Error));
    TestTrue(TEXT("All actual values/records and bound source still exact after refusal controls"), Preserved());

    bool Compiled = false;
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_COLLECTION_COMPILE")) == TEXT("1"))
    {
        if (!TestTrue(TEXT("Collection renderer qualification requires actual SM5"),
                GMaxRHIFeatureLevel == ERHIFeatureLevel::SM5))
            return false;
        const FString Temp = TEXT("/Game/__P4CollectionCompile_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const auto Graph = MakeShared<FJsonObject>();
        Graph->SetNumberField(TEXT("format"), 2);
        Graph->SetStringField(TEXT("domain"), TEXT("MD_Surface"));
        Graph->SetStringField(TEXT("blend_mode"), TEXT("BLEND_Opaque"));
        Graph->SetStringField(TEXT("shading_model"), TEXT("MSM_Unlit"));
        Graph->SetBoolField(TEXT("two_sided"), false);
        auto Nodes = MakeShared<FJsonObject>();
        const auto Node = [&](const TCHAR* Id, FName Name)
        {
            auto N = MakeShared<FJsonObject>();
            N->SetStringField(TEXT("type"), TEXT("CollectionParameter"));
            auto Props = MakeShared<FJsonObject>();
            Props->SetStringField(TEXT("Collection"), Object);
            Props->SetStringField(TEXT("ParameterName"), Name.ToString());
            N->SetObjectField(TEXT("props"), Props);
            Nodes->SetObjectField(Id, N);
        };
        Node(TEXT("Scalar"), Asset->ScalarParameters[0].ParameterName);
        Node(TEXT("Vector"), Asset->VectorParameters[0].ParameterName);
        auto Multiply = MakeShared<FJsonObject>();
        Multiply->SetStringField(TEXT("type"), TEXT("Multiply"));
        auto Inputs = MakeShared<FJsonObject>();
        const auto Pin = [](const TCHAR* Name)
        {
            return TArray<TSharedPtr<FJsonValue>>{
                MakeShared<FJsonValueString>(Name), MakeShared<FJsonValueString>(TEXT(""))};
        };
        Inputs->SetArrayField(TEXT("A"), Pin(TEXT("Scalar")));
        Inputs->SetArrayField(TEXT("B"), Pin(TEXT("Vector")));
        Multiply->SetObjectField(TEXT("inputs"), Inputs);
        Nodes->SetObjectField(TEXT("Result"), Multiply);
        Graph->SetObjectField(TEXT("nodes"), Nodes);
        auto Outputs = MakeShared<FJsonObject>();
        Outputs->SetArrayField(TEXT("EmissiveColor"), Pin(TEXT("Result")));
        Graph->SetObjectField(TEXT("outputs"), Outputs);
        FString Text;
        FJsonSerializer::Serialize(Graph, TJsonWriterFactory<>::Create(&Text));
        TStrongObjectPtr<UMaterial> Material(P4BuildMaterialFromGraph(Text, Temp, Error));
        if (!TestNotNull(
                TEXT("Unsaved scalar/vector consumer uses genuine native expression construction"), Material.Get()))
        {
            AddError(Error);
            return false;
        }
        UPackage* MaterialPackage = Material->GetOutermost();
        ON_SCOPE_EXIT
        {
            FAssetRegistryModule::AssetDeleted(Material.Get());
            Material->ClearFlags(RF_Public | RF_Standalone);
            TestTrue(TEXT("Only the controlled unsaved material retires from its exact namespace"),
                Material->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional));
            Material->MarkPendingKill();
            MaterialPackage->SetDirtyFlag(false);
        };
        int32 CollectionNodes = 0;
        for (auto* Expression : Material->Expressions)
            if (auto* Parameter = Cast<UMaterialExpressionCollectionParameter>(Expression))
            {
                ++CollectionNodes;
                TestTrue(TEXT("Actual expression keeps the genuine collection and exact native parameter Id"),
                    Parameter->Collection == Asset.Get() &&
                        Parameter->ParameterId == Asset->GetParameterId(Parameter->ParameterName));
            }
        TestEqual(TEXT("Both scalar and vector are real collection expressions"), CollectionNodes, 2);
        TArray<FString> Errors;
        Compiled = P4WaitForMaterialShaders(Material.Get(), Errors, Error);
        TestTrue(
            TEXT("Real current-platform shader map compiles actual MPC scalar/vector uniform consumers"), Compiled);
        if (!Compiled)
        {
            AddError(Error);
            for (const auto& Message : Errors)
                AddError(Message);
        }
        TestFalse(TEXT("Controlled native consumer was never saved"),
            IFileManager::Get().FileExists(
                *FPackageName::LongPackageNameToFilename(Temp, FPackageName::GetAssetPackageExtension())));
    }
    // Deliberately last: this isolated gate leaves only a PRIVATE readiness
    // tombstone. No reinitialization of this existing asset is used as proof.
    // Both value and package dirty flag are restored; the notification must
    // nevertheless prevent stale private-layout/default-resource admission.
    const float SavedValue = Asset->ScalarParameters[0].DefaultValue;
    ON_SCOPE_EXIT
    {
        Asset->ScalarParameters[0].DefaultValue = SavedValue;
        Asset->GetOutermost()->SetDirtyFlag(Dirty);
    };
    FCoreUObjectDelegates::OnObjectModified.Broadcast(Asset.Get());
    uint32 ChangedBits = 0;
    FMemory::Memcpy(&ChangedBits, &SavedValue, 4);
    ChangedBits ^= 1u;
    FMemory::Memcpy(&Asset->ScalarParameters[0].DefaultValue, &ChangedBits, 4);
    TestFalse(TEXT("Genuine collection value mutation refuses even without marking its package dirty"),
        P4OriginalMaterialCollectionMatchesSource(Asset.Get(), Request, Binding, Error));
    Asset->ScalarParameters[0].DefaultValue = SavedValue;
    Asset->GetOutermost()->SetDirtyFlag(Dirty);
    TestTrue(TEXT("Restored genuine native values alone still equal source"),
        P4MaterialCollectionValueControl(Asset.Get(), Request, Binding, Error));
    TestFalse(TEXT("Edit/restore with a clean dirty flag cannot reuse private readiness"),
        P4OriginalMaterialCollectionMatchesSource(Asset.Get(), Request, Binding, Error));
    TestTrue(TEXT("Lifecycle refusal is explicit after edit/restore"),
        Error.Contains(TEXT("completed-load identity witness")));
    // Even a subsequent duplicate cold-loaded callback cannot erase a mutation
    // tombstone. This is a real public delegate signal, not a fabricated record.
    FCoreUObjectDelegates::OnAssetLoaded.Broadcast(Asset.Get());
    TestFalse(TEXT("Duplicate loaded callback cannot rehabilitate an edited object"),
        P4MaterialCollectionLifecycleControl(Asset.Get(), Error));
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("package"), Package);
    Report->SetStringField(TEXT("source_sha256"), Binding.SourceDigest);
    Report->SetStringField(TEXT("state_id"), StateHex);
    Report->SetNumberField(TEXT("process_id"), FPlatformProcess::GetCurrentProcessId());
    Report->SetBoolField(TEXT("loaded_from_disk"), Asset->HasAnyFlags(RF_WasLoaded));
    Report->SetBoolField(TEXT("edit_restore_refused"), !P4MaterialCollectionLifecycleControl(Asset.Get(), Error));
    Report->SetNumberField(TEXT("scalar_count"), Asset->ScalarParameters.Num());
    Report->SetNumberField(TEXT("vector_count"), Asset->VectorParameters.Num());
    Report->SetBoolField(TEXT("actual_sm5_compiled"), Compiled);
    Report->SetBoolField(TEXT("source_native_metadata_preserved"), Preserved());
    Report->SetBoolField(TEXT("passed"), !HasAnyErrors());
    FString ReportText;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&ReportText));
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialCollections");
    IFileManager::Get().MakeDirectory(*Folder, true);
    TestTrue(TEXT("Small actual collection proof receipt retained"),
        FFileHelper::SaveStringToFile(
            ReportText, *(Folder / TEXT("source.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return !HasAnyErrors();
}
#endif
