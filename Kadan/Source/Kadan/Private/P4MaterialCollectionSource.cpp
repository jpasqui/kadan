#include "P4MaterialCollectionSource.h"
#include "P4Common.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "P4CreatorSource.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/material_collection_source.h"
THIRD_PARTY_INCLUDES_END
#include "Dom/JsonObject.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeLock.h"
#include "ShaderParameterMetadata.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectThreadContext.h"

namespace
{
FCriticalSection CollectionLifecycleLock;
// False is a tombstone: a later load callback must not rehabilitate an object
// modified by another completion callback. Only the explicit importer can
// replace it after another REAL native PostLoad.
TMap<TWeakObjectPtr<UMaterialParameterCollection>, bool> CompletedCollections;
FDelegateHandle CollectionLoaded, CollectionModified, CollectionChanged;
bool CollectionWitness(UMaterialParameterCollection* Asset)
{
    FScopeLock Lock(&CollectionLifecycleLock);
    const bool* Complete = CompletedCollections.Find(TWeakObjectPtr<UMaterialParameterCollection>(Asset));
    return Complete && *Complete;
}
void CollectionInvalidated(UObject* Object)
{
    if (!Object || Object->GetClass() != UMaterialParameterCollection::StaticClass() ||
        Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
        return;
    FScopeLock Lock(&CollectionLifecycleLock);
    const TWeakObjectPtr<UMaterialParameterCollection> Key(static_cast<UMaterialParameterCollection*>(Object));
    for (auto I = CompletedCollections.CreateIterator(); I; ++I)
        if (!I.Key().IsValid())
            I.RemoveCurrent();
    if (CompletedCollections.Num() < 4096 || CompletedCollections.Contains(Key))
        CompletedCollections.Add(Key, false);
}
bool CollectionLayout(const FShaderParametersMetadata& M, int32 Scalars, int32 Vectors, FString& Error)
{
    Error.Reset();
    const auto Fail = [&]()
    {
        Error = TEXT("Native collection uniform metadata/layout differs from source parameter packing");
        return false;
    };
    if (Scalars < 0 || Vectors < 0 || Scalars > 4096 || Vectors > 4096)
        return Fail();
    const uint32 Count = uint32((Scalars + 3) / 4 + Vectors),
                 Size = Align(Count * 16u, uint32(SHADER_PARAMETER_STRUCT_ALIGNMENT));
    // Exact stock ParameterCollection.cpp::CreateBufferStruct, not a template
    // guessed from the asset name. DataDriven constructor initializes layout.
    if (M.GetUseCase() != FShaderParametersMetadata::EUseCase::DataDrivenUniformBuffer ||
        !P4ExactNativeString(M.GetStructTypeName(), TEXT("MaterialCollection")) ||
        !P4ExactNativeString(M.GetShaderVariableName(), TEXT("MaterialCollection")) ||
        M.GetStaticSlotName() != nullptr || M.GetSize() != Size || M.GetMembers().Num() != 1)
        return Fail();
    const auto& V = M.GetMembers()[0];
    if (!P4ExactNativeString(V.GetName(), TEXT("Vectors")) || !V.GetShaderType() ||
        !P4ExactNativeString(V.GetShaderType(), TEXT("")) || V.GetOffset() != 0 || V.GetBaseType() != UBMT_FLOAT32 ||
        V.GetPrecision() != EShaderPrecisionModifier::Half || V.GetNumRows() != 1 || V.GetNumColumns() != 4 ||
        V.GetNumElements() != Count || V.GetStructMetadata() != nullptr)
        return Fail();
    const auto& L = M.GetLayout();
    if (L.ConstantBufferSize != Size || L.Resources.Num() || L.GraphResources.Num() || L.GraphTextures.Num() ||
        L.GraphBuffers.Num() || L.GraphUniformBuffers.Num() || L.UniformBuffers.Num() ||
        L.RenderTargetsOffset != FRHIUniformBufferLayout::kInvalidOffset || L.bHasNonGraphOutputs ||
        IsUniformBufferStaticSlotValid(L.StaticSlot))
        return Fail();
    return true;
}
bool CollectionLifecycleReady(UMaterialParameterCollection* Asset, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || !IsValid(Asset) || Asset->GetClass() != UMaterialParameterCollection::StaticClass() ||
        Asset->HasAnyFlags(
            RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects) ||
        FUObjectThreadContext::Get().IsRoutingPostLoad || !CollectionWitness(Asset))
    {
        Error = TEXT("Native collection private layout has no unmodified completed-load identity witness");
        return false;
    }
    // Safe only after observed REAL completion, never inferred from arrays or
    // global StateId maps. Exact stock class assigns a non-null struct in
    // PostLoad; its only other writer replaces it in PostEditChangeProperty.
    // That public mutation invalidates the weak witness until an actual reload.
    if (!CollectionLayout(
            Asset->GetUniformBufferStruct(), Asset->ScalarParameters.Num(), Asset->VectorParameters.Num(), Error))
        return false;
    if (!CollectionWitness(Asset))
    {
        Error = TEXT("Native collection changed during uniform layout verification");
        return false;
    }
    return true;
}
bool CollectionValues(UMaterialParameterCollection* Asset, const FP4MaterialUsedResourceRequest& Request,
    const FP4KadanSourceBinding& Binding, FString& Error)
{
    Error.Reset();
    const auto Fail = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    // The project's copy no longer matches the game's file. Reimport keeps a changed original as the
    // creator's, so the way back is a fresh import of the collection. The detail stays for the report.
    const auto Differs = [&](const TCHAR* Why)
    {
        const FString Name = IsValid(Asset) ? Asset->GetName() : FString(TEXT("A parameter collection"));
        Error = FString::Printf(
            TEXT(
                "%s in this project is not the game's copy, so game materials that use it stay in the game. Close Unreal Editor, delete %s.uasset from Content%s, then open the project and try again. %s."),
            *Name, *Name,
            IsValid(Asset) ? *FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName()).Mid(5) : TEXT(""),
            Why);
        return false;
    };
    if (!IsInGameThread() || !IsValid(Asset) || Asset->GetClass() != UMaterialParameterCollection::StaticClass() ||
        Request.Kind != EP4MaterialUsedResourceKind::Collection ||
        !P4ExactNativeString(Request.NativeClass, TEXT("/Script/Engine.MaterialParameterCollection")) ||
        !Request.bSourceAvailable || !P4ExactNativeString(Request.SourceSha256, Binding.SourceDigest) ||
        !P4ExactNativeString(Request.Package, Binding.Package) || !Request.Owners.Num())
        return Fail(TEXT("Shader-used collection lacks exact typed current source/owner identity"));
    if (!P4KadanSourceBindingCurrent(Binding, Error))
        return false;
    try
    {
        const auto Source = P4MapKit::Material::ReadMaterialCollectionSource(
            std::vector<uint8>(Binding.Header.GetData(), Binding.Header.GetData() + Binding.Header.Num()),
            std::vector<uint8>(Binding.Body.GetData(), Binding.Body.GetData() + Binding.Body.Num()),
            std::string(TCHAR_TO_UTF8(*Binding.Package)));
        const auto Guid = [](const std::array<uint32, 4>& G) { return FGuid(G[0], G[1], G[2], G[3]); };
        const auto Bits = [](float V)
        {
            uint32 B = 0;
            FMemory::Memcpy(&B, &V, 4);
            return B;
        };
        const auto NameSame = [](FName A, const P4MapKit::Material::FMaterialCollectionParameterSource& S)
        {
            return P4ExactNativeString(A.ToString(), UTF8_TO_TCHAR(S.Name.c_str())) && A.GetNumber() == S.NameNumber &&
                P4ExactNativeString(A.GetPlainNameString(), UTF8_TO_TCHAR(S.NameBase.c_str()));
        };
        if (Asset->StateId != Guid(Source.State) || Asset->ScalarParameters.Num() != int32(Source.Scalars.size()) ||
            Asset->VectorParameters.Num() != int32(Source.Vectors.size()))
            return Differs(TEXT("Its StateId or parameter count differs from the game's file"));
        TSet<FName> Names;
        TSet<FGuid> Ids;
        for (int32 I = 0; I < Asset->ScalarParameters.Num(); ++I)
        {
            const auto& A = Asset->ScalarParameters[I];
            const auto& S = Source.Scalars[size_t(I)];
            if (!NameSame(A.ParameterName, S) || A.Id != Guid(S.Id) || Bits(A.DefaultValue) != S.Value[0] ||
                Names.Contains(A.ParameterName) || Ids.Contains(A.Id))
                return Differs(TEXT("A scalar parameter's name, ID, order or default differs from the game's file"));
            Names.Add(A.ParameterName);
            Ids.Add(A.Id);
        }
        for (int32 I = 0; I < Asset->VectorParameters.Num(); ++I)
        {
            const auto& A = Asset->VectorParameters[I];
            const auto& S = Source.Vectors[size_t(I)];
            const float V[] = {A.DefaultValue.R, A.DefaultValue.G, A.DefaultValue.B, A.DefaultValue.A};
            if (!NameSame(A.ParameterName, S) || A.Id != Guid(S.Id) || Names.Contains(A.ParameterName) ||
                Ids.Contains(A.Id))
                return Differs(TEXT("A vector parameter's name, ID or order differs from the game's file"));
            for (int32 C = 0; C < 4; ++C)
                if (Bits(V[C]) != S.Value[size_t(C)])
                    return Differs(TEXT("A vector parameter's default differs from the game's file"));
            Names.Add(A.ParameterName);
            Ids.Add(A.Id);
        }
        const FString State = UTF8_TO_TCHAR(P4MapKit::Material::MaterialCollectionStateHex(Source.State).c_str());
        for (const auto& Owner : Request.Owners)
        {
            if (!Owner.Consumers.Num())
                return Fail(TEXT("Shader-used collection has no actual consumer"));
            for (const auto& Use : Owner.Consumers)
            {
                if (!P4ExactNativeString(Use.NodeType, TEXT("CollectionParameter")) || Use.Parameter.IsEmpty() ||
                    !Use.CollectionUniforms.Num() || Use.CollectionUniforms.Num() != Use.CollectionStateIds.Num())
                    return Fail(TEXT("Shader-used collection consumer binding is incomplete"));
                for (const FString& Id : Use.CollectionStateIds)
                    if (!P4ExactNativeString(Id, State))
                        return Fail(TEXT("Shader collection uniform StateId differs from exact source"));
                const FName Name(*Use.Parameter);
                const FGuid Id = Asset->GetParameterId(Name);
                if (!Names.Contains(Name) || !Id.IsValid())
                    return Fail(TEXT("Shader-used collection parameter is absent from native source"));
                int32 Index = INDEX_NONE, Component = INDEX_NONE;
                Asset->GetParameterIndex(Id, Index, Component);
                int32 ScalarIndex = INDEX_NONE, VectorIndex = INDEX_NONE;
                for (size_t I = 0; I < Source.Scalars.size(); ++I)
                    if (P4ExactNativeString(Use.Parameter, UTF8_TO_TCHAR(Source.Scalars[I].Name.c_str())))
                        ScalarIndex = int32(I);
                for (size_t I = 0; I < Source.Vectors.size(); ++I)
                    if (P4ExactNativeString(Use.Parameter, UTF8_TO_TCHAR(Source.Vectors[I].Name.c_str())))
                        VectorIndex = int32(I);
                // Stock 4.26 ParameterCollection.cpp::GetParameterIndex and
                // CollectionInstance::GetParameterData use this exact packing.
                if (ScalarIndex != INDEX_NONE)
                {
                    if (Id != Guid(Source.Scalars[size_t(ScalarIndex)].Id) || Index != ScalarIndex / 4 ||
                        Component != ScalarIndex % 4)
                        return Fail(TEXT("Native scalar uniform index differs from source parameter order"));
                }
                else if (VectorIndex != INDEX_NONE)
                {
                    if (Id != Guid(Source.Vectors[size_t(VectorIndex)].Id) ||
                        Index != (int32(Source.Scalars.size()) + 3) / 4 + VectorIndex || Component != INDEX_NONE)
                        return Fail(TEXT("Native vector uniform index differs from source parameter order"));
                }
                else
                    return Fail(TEXT("Shader parameter spelling differs from exact source"));
            }
        }
        return P4KadanSourceBindingCurrent(Binding, Error);
    }
    catch (const std::bad_alloc&)
    {
        throw;
    }
    catch (const std::exception& E)
    {
        Error = TEXT("Material collection source refused: ") + FString(UTF8_TO_TCHAR(E.what()));
        return false;
    }
}
} //namespace

namespace
{
void CollectionCompleted(UMaterialParameterCollection* Asset, bool ExplicitImport)
{
    if (!IsInGameThread() || !IsValid(Asset) || Asset->GetClass() != UMaterialParameterCollection::StaticClass() ||
        Asset->HasAnyFlags(
            RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects) ||
        FUObjectThreadContext::Get().IsRoutingPostLoad)
        return;
    FScopeLock Lock(&CollectionLifecycleLock);
    for (auto I = CompletedCollections.CreateIterator(); I; ++I)
        if (!I.Key().IsValid())
            I.RemoveCurrent();
    const TWeakObjectPtr<UMaterialParameterCollection> Key(Asset);
    if (!ExplicitImport)
    {
        const bool* Previous = CompletedCollections.Find(Key);
        if (Previous && !*Previous)
            return;
    }
    // Weak, per-process, bounded identities. Overflow refuses readiness rather
    // than inventing a lifecycle or keeping assets alive.
    if (CompletedCollections.Num() < 4096 || CompletedCollections.Contains(Key))
        CompletedCollections.Add(Key, true);
}
} //namespace
void P4MaterialCollectionPostLoadCompleted(UMaterialParameterCollection* Asset)
{
    CollectionCompleted(Asset, true);
}
void P4StartMaterialCollectionReadiness()
{
    // Stock 4.26 UObjectGlobals.cpp::EndLoad 1625/1647 routes and completes
    // ConditionalPostLoad before its final asset callbacks at 1777. AsyncLoading
    // ProcessLoadedPackages 4476..4492 similarly waits for Complete; this is
    // NOT UObject::Super::PostLoad (which would still be inside MPC::PostLoad).
    if (!CollectionLoaded.IsValid())
        CollectionLoaded = FCoreUObjectDelegates::OnAssetLoaded.AddLambda(
            [](UObject* Object)
            {
                if (Object && Object->GetClass() == UMaterialParameterCollection::StaticClass() &&
                    Object->HasAnyFlags(RF_WasLoaded))
                    CollectionCompleted(static_cast<UMaterialParameterCollection*>(Object), false);
            });
    if (!CollectionModified.IsValid())
        CollectionModified = FCoreUObjectDelegates::OnObjectModified.AddStatic(&CollectionInvalidated);
    if (!CollectionChanged.IsValid())
        CollectionChanged = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
            [](UObject* Object, FPropertyChangedEvent&) { CollectionInvalidated(Object); });
}
void P4StopMaterialCollectionReadiness()
{
    FCoreUObjectDelegates::OnAssetLoaded.Remove(CollectionLoaded);
    CollectionLoaded.Reset();
    FCoreUObjectDelegates::OnObjectModified.Remove(CollectionModified);
    CollectionModified.Reset();
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(CollectionChanged);
    CollectionChanged.Reset();
    FScopeLock Lock(&CollectionLifecycleLock);
    CompletedCollections.Reset();
}

bool P4OriginalMaterialCollectionMatchesSource(UMaterialParameterCollection* Asset,
    const FP4MaterialUsedResourceRequest& Request, const FP4KadanSourceBinding& Binding, FString& Error)
{
    if (!CollectionValues(Asset, Request, Binding, Error))
        return false;
    FP4OriginalRecord Record;
    // Each condition names itself, so a refusal says which part of the proof is missing.
    const TCHAR* Missing = Asset->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad) ? TEXT("it is not loaded")
        : Asset->GetOutermost()->IsDirty()                                   ? TEXT("its package has unsaved changes")
        : P4HasCreatorSource(Asset)                                          ? TEXT("it has a creator source")
        : !P4ExactNativeString(Asset->GetPathName(), Request.Object)
        ? TEXT("its path differs from the shader's request")
        : nullptr;
    FString Read;
    if (!Missing && !P4ReadOriginalRecord(Asset, Record, Read))
        Missing = TEXT("its original record cannot be read");
    if (!Missing)
        Missing = Record.Version != 2                                 ? TEXT("its original record is not version 2")
            : !P4ExactNativeString(Record.Package, Binding.Package)   ? TEXT("its record names another package")
            : !P4ExactNativeString(Record.Class, Request.NativeClass) ? TEXT("its record names another class")
            : !P4ExactNativeString(Record.Fidelity, TEXT("exact"))    ? TEXT("its record is not exact")
            : Record.ReferenceOnly                                    ? TEXT("it is kept for reference only")
            : !Record.ReadOnly                                        ? TEXT("it is not read-only")
            : !P4ExactNativeString(Record.Kind, TEXT("object"))       ? TEXT("its record is not an object record")
            : !P4ExactNativeString(Record.ComparisonSchema, TEXT("object-lines-v1"))
            ? TEXT("its record has another comparison schema")
            : !P4ExactNativeString(Record.SourceDigestKind, TEXT("header-body-sha256-v1"))
            ? TEXT("its record has another digest kind")
            : !P4ExactNativeString(Record.SourceDigest, Binding.SourceDigest)
            ? TEXT("its source digest differs from the game's file")
            : Record.Values.Num() ? TEXT("its record holds values")
                                  : nullptr;
    if (Missing)
    {
        Error = FString(TEXT("Shader-used collection has no clean complete original native import proof (")) + Missing +
            (Read.IsEmpty() ? TEXT("") : *(TEXT(": ") + Read)) + TEXT("); existing content is preserved");
        return false;
    }
    UMetaData* Meta = Asset->GetOutermost()->MetaData;
    const FString Text = Meta ? Meta->GetValue(Asset, TEXT("P4NativeObjectBaseline")) : FString();
    TSharedPtr<FJsonObject> Baseline;
    double Format = 0, Bytes = -1, Values = -1;
    FString Owner, Package, Source, State;
    if (!P4ParseJson(Text, Baseline) || !Baseline->TryGetNumberField(TEXT("format"), Format) || Format != 1 ||
        !Baseline->TryGetStringField(TEXT("owner"), Owner) ||
        !P4ExactNativeString(Owner, TEXT("p4mapkit-original-object-v1")) ||
        !Baseline->TryGetStringField(TEXT("package"), Package) || !P4ExactNativeString(Package, Binding.Package) ||
        !Baseline->TryGetStringField(TEXT("source_sha256"), Source) ||
        !P4ExactNativeString(Source, Binding.SourceDigest) || !Baseline->TryGetStringField(TEXT("state"), State) ||
        State.Len() != 64 || !P4ExactNativeString(State, Record.ComparisonDigest) ||
        !Baseline->TryGetNumberField(TEXT("reference_only_bytes"), Bytes) || Bytes != 0 ||
        !Baseline->TryGetNumberField(TEXT("reference_only_values"), Values) || Values != 0)
    {
        Error = TEXT("Shader-used collection native source/cook baseline is missing, partial or differs");
        return false;
    }
    for (TCHAR C : State)
        if (!((C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('a') && C <= TEXT('f'))))
        {
            Error = TEXT("Shader-used collection native baseline digest is malformed");
            return false;
        }
    if (Meta && FString(Meta->GetValue(Asset, TEXT("P4NativeObjectValuesNotHeld"))).Len())
    {
        Error = TEXT("Shader-used collection retains unsupported source-value notes");
        return false;
    }
    if (!CollectionLifecycleReady(Asset, Error))
        return false;
    return P4KadanSourceBindingCurrent(Binding, Error);
}
#if P4_KADAN_TEST_SEAMS
bool P4MaterialCollectionValueControl(UMaterialParameterCollection* Asset,
    const FP4MaterialUsedResourceRequest& Request, const FP4KadanSourceBinding& Binding, FString& Error)
{
    return CollectionValues(Asset, Request, Binding, Error);
}
bool P4MaterialCollectionLayoutControl(const FShaderParametersMetadata& M, int32 Scalars, int32 Vectors, FString& Error)
{
    return CollectionLayout(M, Scalars, Vectors, Error);
}
bool P4MaterialCollectionLifecycleControl(UMaterialParameterCollection* Asset, FString& Error)
{
    return CollectionLifecycleReady(Asset, Error);
}
#endif
