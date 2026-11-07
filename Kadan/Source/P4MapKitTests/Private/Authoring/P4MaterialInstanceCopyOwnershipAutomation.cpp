#include "P4CreatorSource.h"
#include "P4ExportDriver.h"
#include "P4ExportSelection.h"
#include "P4KadanAssetPolicy.h"
#include "P4KadanGameContent.h"
#include "P4KadanReadOnly.h"
#include "P4LevelContent.h"
#include "P4MaterialInstance.h"
#include "P4MaterialInstanceComparison.h"
#include "P4MaterialInstanceCopyContent.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "P4PackageSave.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "IAssetTools.h"
#include "Curves/CurveFloat.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Materials/MaterialInstanceConstant.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "StaticParameterSet.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
// Source-only control pending the separately reviewed copied-content contract.
// It cannot mint a recovered-original baseline or relax original edit policy.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialInstanceCopyOwnership, "P4MapKit.Kadan.MaterialInstanceCopyOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialInstanceCopyOwnership::RunTest(const FString&)
{
    const FString Target = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MIC_COPY_PROJECT"));
    const FString RawRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MIC_COPY_RAW_ROOT"));
    const FString ExpectedSaved = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MIC_COPY_SAVED_SHA256"));
    const FString ExpectedRaw = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MIC_COPY_RAW_SHA256"));
    if (Target.IsEmpty() || RawRoot.IsEmpty() || ExpectedSaved.IsEmpty() || ExpectedRaw.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. A separately owned real source-bound MIC copy fixture is required."));
        return true;
    }
    const auto Directory = [](FString Path)
    {
        Path = FPaths::ConvertRelativePathToFull(Path);
        FPaths::RemoveDuplicateSlashes(Path);
        FPaths::NormalizeDirectoryName(Path);
        return Path;
    };
    if (!TestTrue(TEXT("Only the explicit owned project can create this copy control"),
            FPaths::IsSamePath(Directory(Target), Directory(FPaths::ProjectDir())) &&
                !FPaths::IsSamePath(Directory(RawRoot), Directory(FPaths::ProjectContentDir())) &&
                !FPaths::IsUnderDirectory(Directory(RawRoot), Directory(FPaths::ProjectContentDir()))))
        return false;
    const FString ResultFile = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialInstanceCopy/result.json");
    if (!TestTrue(TEXT("A prior copy-control receipt must be preserved before this selector can run"),
            !IFileManager::Get().FileExists(*ResultFile) && !IFileManager::Get().DirectoryExists(*ResultFile)))
        return false;
    auto Receipt = MakeShared<FJsonObject>();
    ON_SCOPE_EXIT
    {
        Receipt->SetBoolField(TEXT("passed"), !HasAnyErrors());
        Receipt->SetBoolField(TEXT("original_baseline_created_or_replaced"), false);
        Receipt->SetBoolField(TEXT("actual_mod_export_attempted"), false);
        FString Text;
        FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<TCHAR>::Create(&Text));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ResultFile), true);
        if (!FFileHelper::SaveStringToFile(Text, *ResultFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
                &IFileManager::Get(), FILEWRITE_NoReplaceExisting))
            AddError(TEXT("The copy-control receipt could not be saved without replacing prior evidence."));
    };
    const FString Package = TEXT("/Game/Carrot4/Objects/Gimmicks/Bubble/MI_stg_Bubble_Frozen_WH01");
    const FString Path = Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
    const auto FileHash = [](const FString& File)
    {
        int64 Bytes = 0;
        FString Digest;
        return P4HashFile(File, 32 * 1024 * 1024, Bytes, Digest) ? Digest : FString();
    };
    FString Saved;
    if (!FPackageName::DoesPackageExist(Package, nullptr, &Saved) ||
        !TestTrue(TEXT("The actual saved source fixture matches its controller hardpin"),
            P4ExactNativeString(FileHash(Saved), ExpectedSaved)))
        return false;
    TArray<uint8> Header, Body;
    FString RawSha;
    const FString RawFile = RawRoot / (Package.Mid(6) + TEXT(".uasset"));
    if (!TestTrue(TEXT("The exact current raw pair remains the source named by the retained record"),
            FFileHelper::LoadFileToArray(Header, *RawFile) &&
                FFileHelper::LoadFileToArray(Body, *FPaths::ChangeExtension(RawFile, TEXT("uexp"))) &&
                P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), RawSha) &&
                P4ExactNativeString(RawSha, ExpectedRaw)))
        return false;
    const FString HeaderHash = FileHash(RawFile), BodyFile = FPaths::ChangeExtension(RawFile, TEXT("uexp")),
                  BodyHash = FileHash(BodyFile);
    ON_SCOPE_EXIT
    {
        TestTrue(TEXT("The real current raw source pair remains byte-exact"),
            P4ExactNativeString(FileHash(RawFile), HeaderHash) && P4ExactNativeString(FileHash(BodyFile), BodyHash));
    };
    TStrongObjectPtr<UMaterialInstanceConstant> Original(LoadObject<UMaterialInstanceConstant>(nullptr, *Path));
    FP4OriginalRecord Record;
    FString Error, Origin;
    const auto SourceBound = [&]()
    {
        if (P4ExactNativeString(Record.SourceDigestKind, TEXT("header-body-sha256-v1")))
            return P4ExactNativeString(Record.SourceDigest, RawSha);
        if (!P4ExactNativeString(Record.SourceDigestKind, TEXT("material-instance-v2-md5")) ||
            !P4ExactNativeString(Record.ComparisonSchema, TEXT("material-instance-native-state-v1")))
            return false;
        FP4MaterialInstanceNativeEvidence Evidence;
        FString Why;
        const FString Current = P4MaterialInstanceComparisonState(Original.Get(), Why, &Evidence);
        return Why.IsEmpty() && P4ExactNativeString(Current, Record.ComparisonDigest) &&
            P4ExactNativeString(Evidence.SourceSha256, RawSha);
    };
    if (!TestTrue(TEXT("A real complete original supplies the copy; the test invents no baseline"),
            Original.IsValid() && Original->GetClass() == UMaterialInstanceConstant::StaticClass() &&
                P4ReadOriginalRecord(Original.Get(), Record, Error) && Record.Version == 2 && Record.ReadOnly &&
                !Record.ReferenceOnly && Record.Values.Num() == 0 && SourceBound() &&
                P4HasIntrinsicGameSource(Original.Get(), &Origin) && P4ExactNativeString(Origin, Package) &&
                !P4HasIntrinsicGameProxy(Original.Get())))
        return false;
    const auto SupportedParameters = [](UMaterialInstanceConstant* Instance)
    {
        const auto Override = [](const auto& Value) { return Value.bOverride; };
        const FStaticParameterSet& Static = Instance->GetStaticParameters();
        const auto& Base = Instance->BasePropertyOverrides;
        if (!Instance->Parent || Instance->bOverrideSubsurfaceProfile || !Static.IsEmpty() ||
            Instance->RuntimeVirtualTextureParameterValues.Num() || Instance->FontParameterValues.Num() ||
            Instance->bHasStaticPermutationResource || Static.StaticSwitchParameters.ContainsByPredicate(Override) ||
            Static.StaticComponentMaskParameters.ContainsByPredicate(Override) ||
            Static.TerrainLayerWeightParameters.ContainsByPredicate(Override) ||
            Static.MaterialLayersParameters.ContainsByPredicate(Override) || Base.bOverride_BlendMode ||
            Base.bOverride_ShadingModel || Base.bOverride_TwoSided || Base.bOverride_OpacityMaskClipValue ||
            Base.bOverride_DitheredLODTransition || Base.bOverride_CastDynamicShadowAsMasked)
            return false;
        const auto Plain = [](const auto& Value)
        {
            return Value.ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter &&
                Value.ParameterInfo.Index == INDEX_NONE && Value.ParameterInfo.Name != FName(TEXT("C4SkyLightParam"));
        };
        for (const auto& Value : Instance->ScalarParameterValues)
            if (!Plain(Value) || Value.AtlasData.bIsUsedAsAtlasPosition || !Value.AtlasData.Curve.IsNull() ||
                !Value.AtlasData.Atlas.IsNull())
                return false;
        for (const auto& Value : Instance->VectorParameterValues)
            if (!Plain(Value))
                return false;
        for (const auto& Value : Instance->TextureParameterValues)
            if (!Plain(Value))
                return false;
        TArray<FMaterialParameterInfo> ParentInfo;
        TArray<FGuid> ParentIds;
        Instance->Parent->GetAllScalarParameterInfo(ParentInfo, ParentIds);
        return Instance->ScalarParameterValues.ContainsByPredicate(
            [&](const auto& Value)
            {
                return Value.ParameterInfo.Name == FName(TEXT("EmissiveIntensity")) &&
                    ParentInfo.Contains(Value.ParameterInfo);
            });
    };
    if (!TestTrue(TEXT("The actual loaded source has supported ordinary parameters, with no static/base restriction"),
            SupportedParameters(Original.Get())))
        return false;
    UMetaData* Meta = Original->GetOutermost()->GetMetaData();
    if (!TestFalse(
            TEXT("This finite ordinary copy control refuses every unsupported transport presence, including empty"),
            Meta->HasValue(Original.Get(), TEXT("P4MaterialInstanceTransportV2"))))
        return false;
    const FString Marker = Meta->GetValue(Original.Get(), TEXT("P4OriginalRecord"));
    const bool HadNativeSource = Meta->HasValue(Original.Get(), TEXT("P4MaterialInstanceNativeSourceV1"));
    const FString NativeSource = Meta->GetValue(Original.Get(), TEXT("P4MaterialInstanceNativeSourceV1"));
    const FString Before = P4MaterialInstanceJson(Original.Get(), Error);
    TArray<FString> BeforeLines;
    const FString OriginalState = P4OriginalObjectAssetStateAndLines(Original.Get(), BeforeLines);
    TArray<TSharedPtr<FJsonValue>> NativeLines;
    for (const FString& Line : BeforeLines)
        NativeLines.Add(MakeShared<FJsonValueString>(Line));
    Receipt->SetArrayField(TEXT("source_current_native_lines"), NativeLines);
    TArray<TSharedPtr<FJsonValue>> Fields;
    for (TFieldIterator<FProperty> It(Original->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        const FProperty* Property = *It;
        auto Field = MakeShared<FJsonObject>();
        Field->SetStringField(TEXT("owner"), Property->GetOwnerStruct()->GetPathName());
        Field->SetStringField(TEXT("name"), Property->GetName());
        Field->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
        Field->SetNumberField(TEXT("array_dim"), Property->ArrayDim);
        Field->SetNumberField(TEXT("element_size"), Property->ElementSize);
        const bool Persistent = !Property->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization);
        Field->SetBoolField(TEXT("persistent"), Persistent);
        if (Persistent)
        {
            TArray<TSharedPtr<FJsonValue>> Values;
            for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
            {
                FString Value;
                Property->ExportTextItem(Value, Property->ContainerPtrToValuePtr<void>(Original.Get(), Index), nullptr,
                    Original.Get(), PPF_Delimited);
                Values.Add(MakeShared<FJsonValueString>(Value));
            }
            Field->SetArrayField(TEXT("actual_property_text"), Values);
        }
        Fields.Add(MakeShared<FJsonValueObject>(Field));
    }
    Receipt->SetArrayField(TEXT("actual_inherited_reflected_field_inventory"), Fields);
    Receipt->SetStringField(TEXT("source_package"), Package);
    Receipt->SetStringField(TEXT("source_raw_sha256"), RawSha);
    Receipt->SetStringField(TEXT("source_saved_sha256"), ExpectedSaved);
    Receipt->SetStringField(TEXT("source_current_native_state_before"), OriginalState);
    if (!TestTrue(TEXT("The original remains locked and its complete current native state is readable"),
            Error.IsEmpty() && !Before.IsEmpty() && OriginalState.Len() == 64 &&
                !P4Kadan::GetAssetPolicy(Original.Get()).AllowEdit && P4Kadan::IsLockedObject(Original.Get())))
        return false;
    ON_SCOPE_EXIT
    {
        FString Why;
        TestTrue(TEXT("The original values, complete native state, source marker and saved bytes are unchanged"),
            P4ExactNativeString(P4MaterialInstanceJson(Original.Get(), Why), Before) && Why.IsEmpty() &&
                P4ExactNativeString(P4OriginalObjectAssetState(Original.Get()), OriginalState) &&
                P4ExactNativeString(Meta->GetValue(Original.Get(), TEXT("P4OriginalRecord")), Marker) &&
                Meta->HasValue(Original.Get(), TEXT("P4MaterialInstanceNativeSourceV1")) == HadNativeSource &&
                P4ExactNativeString(
                    Meta->GetValue(Original.Get(), TEXT("P4MaterialInstanceNativeSourceV1")), NativeSource) &&
                P4ExactNativeString(FileHash(Saved), ExpectedSaved) &&
                !P4Kadan::GetAssetPolicy(Original.Get()).AllowEdit);
    };
    const auto SourceDiagnostic = P4DiagnoseOriginalAsset(Original.Get());
    Receipt->SetStringField(TEXT("source_original_diagnostic_error"), SourceDiagnostic.Error);
    if (!TestTrue(
            TEXT(
                "The current original source must pass its existing comparison; copy tests cannot invent source authority"),
            SourceDiagnostic.Error.IsEmpty() && SourceDiagnostic.Comparison == EP4OriginalComparison::Equal))
        return false;
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("mic.copy.proof");
    Mod.Name = TEXT("MIC Copy Proof");
    Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    if (!P4ModDerivedNames(Mod, Names, Error))
    {
        AddError(Error);
        return false;
    }
    const FString Name = TEXT("MI_Copy_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UMaterialInstanceConstant> Copy(
        Cast<UMaterialInstanceConstant>(Tools.DuplicateAsset(Name, Names.PackageRoot, Original.Get())));
    if (!TestNotNull(TEXT("Ordinary AssetTools DuplicateAsset creates the creator copy"), Copy.Get()))
        return false;
    UMetaData* CopyMeta = Copy->GetOutermost()->GetMetaData();
    ON_SCOPE_EXIT
    {
        if (Copy.IsValid())
        {
            UPackage* Owned = Copy->GetOutermost();
            FAssetRegistryModule::AssetDeleted(Copy.Get());
            Copy->ClearFlags(RF_Public | RF_Standalone);
            Copy->Rename(
                nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
            Copy->MarkPendingKill();
            Owned->SetDirtyFlag(false);
        }
    };
    const auto EditableCopy = [&]()
    {
        FString From;
        return Copy.IsValid() && P4Kadan::GetAssetPolicy(Copy.Get()).AllowEdit &&
            !P4Kadan::IsLockedObject(Copy.Get()) && P4IsCreatorAsset(Copy.Get()) && !P4HasCreatorSource(Copy.Get()) &&
            P4HasIntrinsicGameSource(Copy.Get(), &From) && P4ExactNativeString(From, Package);
    };
    TestTrue(
        TEXT("The authentic copied origin permits editing without inventing positive creator source"), EditableCopy());
    TestTrue(TEXT("The copy retains the source's actual supported parameter shape"), SupportedParameters(Copy.Get()));
    TArray<TSharedPtr<FJsonValue>> Observations;
    const auto Compare = [&](const TCHAR* Label)
    {
        const auto Result = P4CompareCopiedMaterialInstanceContent(Copy.Get());
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("label"), Label);
        Row->SetNumberField(TEXT("state"), int32(Result.State));
        Row->SetStringField(TEXT("error"), Result.Error);
        Row->SetStringField(TEXT("original_package"), Result.OriginalPackage);
        Observations.Add(MakeShared<FJsonValueObject>(Row));
        Receipt->SetArrayField(TEXT("copy_content_observations"), Observations);
        return Result;
    };
    IInterface_AssetUserData* UserData = Cast<IInterface_AssetUserData>(Copy.Get());
    const UObject* CopyRole = UserData ? UserData->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass())
                                       : nullptr;
    if (!TestNotNull(TEXT("The actual duplicate carries its genuine intrinsic role"), CopyRole))
        return false;
    const bool BoundaryControls = P4MaterialInstanceCopyBoundaryControls(Copy.Get(), CopyRole, Error);
    Receipt->SetStringField(TEXT("native_boundary_control_error"), Error);
    TestTrue(
        TEXT(
            "The actual cook rejects unknown reflected/tag schema/kinds, native templates, extra exports and noncanonical tails"),
        BoundaryControls && Error.IsEmpty());
    if (!TestTrue(TEXT("The authentic unchanged duplicate has a readable complete closed content frame"),
            Compare(TEXT("unchanged")).State == EP4CopiedMICContent::Equal))
        return false;
    FString Same;
    TestTrue(TEXT("The separate copy content contract identifies the exact unchanged game source"),
        P4FindGameContentMatch(Copy.Get(), Same) && P4ExactNativeString(Same, Package));
    TestFalse(TEXT("Full Review refuses an unchanged supported MIC copy"),
        P4ExportAssetAvailability(Copy.Get(), Mod).IsAvailable());
    for (const TCHAR* GuidFieldName : {TEXT("LightingGuid"), TEXT("ParameterStateId")})
    {
        const auto* Guid = FindFProperty<FStructProperty>(Copy->GetClass(), GuidFieldName);
        if (!TestTrue(TEXT("Both named compiler GUID fields have their actual native schema"),
                Guid && Guid->Struct == TBaseStructure<FGuid>::Get()))
            return false;
        FGuid* Value = Guid->ContainerPtrToValuePtr<FGuid>(Copy.Get());
        const FGuid BeforeGuid = *Value;
        *Value = FGuid::NewGuid();
        TestTrue(TEXT("A top-level compiler GUID edit alone is not meaningful copy content"),
            Compare(GuidFieldName).State == EP4CopiedMICContent::Equal);
        TestFalse(TEXT("A top-level compiler GUID edit alone does not make Review pass"),
            P4ExportAssetAvailability(Copy.Get(), Mod).IsAvailable());
        *Value = BeforeGuid;
    }
    const int32 At = Copy->ScalarParameterValues.IndexOfByPredicate(
        [](const auto& Value) { return Value.ParameterInfo.Name == FName(TEXT("EmissiveIntensity")); });
    if (At == INDEX_NONE)
    {
        AddError(TEXT("The named control scalar disappeared during ordinary duplication."));
        return false;
    }
    const FScalarParameterValue Scalar = Copy->ScalarParameterValues[At];
    Copy->ScalarParameterValues[At].ExpressionGUID = FGuid::NewGuid();
    TestTrue(TEXT("A known parameter GUID-only edit is unchanged under the separate meaningful-content contract"),
        Compare(TEXT("known-GUID-only")).State == EP4CopiedMICContent::Equal);
    TestFalse(TEXT("A GUID-only edit does not make otherwise unchanged content eligible"),
        P4ExportAssetAvailability(Copy.Get(), Mod).IsAvailable());
    Copy->ScalarParameterValues[At] = Scalar;
    TArray<FMaterialParameterInfo> ParentScalars;
    TArray<FGuid> ParentScalarIds;
    Copy->Parent->GetAllScalarParameterInfo(ParentScalars, ParentScalarIds);
    const int32 Unknown = Copy->ScalarParameterValues.IndexOfByPredicate(
        [&](const auto& Value) { return !ParentScalars.Contains(Value.ParameterInfo); });
    if (!TestTrue(TEXT("This real fixture supplies an unbound parameter for the separate GUID/value negatives"),
            Unknown != INDEX_NONE))
        return false;
    const FScalarParameterValue Unbound = Copy->ScalarParameterValues[Unknown];
    Copy->ScalarParameterValues[Unknown].ExpressionGUID = FGuid::NewGuid();
    TestTrue(TEXT("An unbound parameter GUID-only edit cannot become meaningful copy content"),
        Compare(TEXT("unbound-GUID-only")).State == EP4CopiedMICContent::Equal);
    TestFalse(TEXT("An unbound GUID-only copy remains unavailable"),
        P4ExportAssetAvailability(Copy.Get(), Mod).IsAvailable());
    Copy->ScalarParameterValues[Unknown] = Unbound;
    Copy->ScalarParameterValues[Unknown].ParameterValue += 0.125f;
    TestTrue(TEXT("An unbound value edit fails closed rather than granting supported edit eligibility"),
        Compare(TEXT("unbound-value-edit")).State == EP4CopiedMICContent::Unreadable);
    Copy->ScalarParameterValues[Unknown] = Unbound;
    Copy->SetScalarParameterValueEditorOnly(Scalar.ParameterInfo, Scalar.ParameterValue + 0.125f);
    TestTrue(TEXT("A meaningful ordinary scalar edit is retained on the editable copy"),
        Copy->ScalarParameterValues[At].ParameterValue == Scalar.ParameterValue + 0.125f && EditableCopy());
    TestTrue(TEXT("Full Review admits the meaningful supported edit through the creator-MIC route"),
        P4ExportAssetAvailability(Copy.Get(), Mod).Route == EP4ExportAssetRoute::CreatorMaterialInstance);
    const float InactiveClip = Copy->BasePropertyOverrides.OpacityMaskClipValue;
    Copy->BasePropertyOverrides.OpacityMaskClipValue += 0.125f;
    TestTrue(TEXT("Even an inactive persistent base value remains strict in a scalar-edited copy"),
        Compare(TEXT("inactive-base-value-edit")).State == EP4CopiedMICContent::Unreadable);
    Copy->BasePropertyOverrides.OpacityMaskClipValue = InactiveClip;
    Copy->ScalarParameterValues[At].ParameterInfo.Index = 0;
    TestTrue(TEXT("Changing parameter identity/index cannot authorize an edit"),
        Compare(TEXT("parameter-index-edit")).State == EP4CopiedMICContent::Unreadable);
    Copy->ScalarParameterValues[At].ParameterInfo = Scalar.ParameterInfo;
    UCurveFloat* Extra = NewObject<UCurveFloat>(Copy.Get());
    if (!TestNotNull(TEXT("The extra-object negative uses a concrete native class"), Extra))
        return false;
    TestTrue(TEXT("An additional persistent child cannot hide behind an otherwise supported scalar edit"),
        Compare(TEXT("additional-child")).State == EP4CopiedMICContent::Unreadable);
    Extra->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
    Extra->MarkPendingKill();
    const bool OverrideProfile = Copy->bOverrideSubsurfaceProfile;
    Copy->bOverrideSubsurfaceProfile = !OverrideProfile;
    TestFalse(TEXT("An unsupported inherited-field change cannot make the scalar-only copied-content contract pass"),
        P4ExportAssetAvailability(Copy.Get(), Mod).IsAvailable());
    Copy->bOverrideSubsurfaceProfile = OverrideProfile;
    const bool HadHistory = CopyMeta->HasValue(Copy.Get(), TEXT("P4KadanCopiedGameAsset"));
    const FString PriorHistory = CopyMeta->GetValue(Copy.Get(), TEXT("P4KadanCopiedGameAsset"));
    CopyMeta->SetValue(Copy.Get(), TEXT("P4KadanCopiedGameAsset"), TEXT("{"));
    TestTrue(TEXT("Malformed present history cannot fall back to the weaker ordinary-copy route"),
        Compare(TEXT("malformed-copy-history")).State == EP4CopiedMICContent::Unreadable);
    if (HadHistory)
        CopyMeta->SetValue(Copy.Get(), TEXT("P4KadanCopiedGameAsset"), *PriorHistory);
    else
        CopyMeta->RemoveValue(Copy.Get(), TEXT("P4KadanCopiedGameAsset"));
    const bool HadTransport = CopyMeta->HasValue(Copy.Get(), TEXT("P4MaterialInstanceTransportV2"));
    const FString Transport = CopyMeta->GetValue(Copy.Get(), TEXT("P4MaterialInstanceTransportV2"));
    CopyMeta->SetValue(Copy.Get(), TEXT("P4MaterialInstanceTransportV2"), TEXT(""));
    TestTrue(TEXT("The detailed native copy diagnostic retains the unsupported transport refusal"),
        Compare(TEXT("present-empty-transport")).State == EP4CopiedMICContent::Unreadable);
    TestFalse(TEXT("Present-empty unsupported transport fails Review even with a meaningful scalar edit"),
        P4ExportAssetAvailability(Copy.Get(), Mod).IsAvailable());
    if (HadTransport)
        CopyMeta->SetValue(Copy.Get(), TEXT("P4MaterialInstanceTransportV2"), *Transport);
    else
        CopyMeta->RemoveValue(Copy.Get(), TEXT("P4MaterialInstanceTransportV2"));
    Copy->ScalarParameterValues[At] = Scalar;
    TestFalse(TEXT("Restoring the scalar restores unchanged-copy refusal"),
        P4ExportAssetAvailability(Copy.Get(), Mod).IsAvailable());
    // Replay the metadata input of the distinct package-copy route using the
    // actual source text. This is not a claim to have exercised that UI route.
    CopyMeta->SetValue(Copy.Get(), TEXT("P4OriginalRecord"), *Marker);
    if (HadNativeSource)
        CopyMeta->SetValue(Copy.Get(), TEXT("P4MaterialInstanceNativeSourceV1"), *NativeSource);
    P4Kadan::UpdateReadOnlyAsset(Copy.Get());
    TSharedPtr<FJsonObject> History;
    const TSharedPtr<FJsonObject>* Records = nullptr;
    TestTrue(TEXT("Metadata-preserving copies archive exact source history and remain editable"),
        !CopyMeta->HasValue(Copy.Get(), TEXT("P4OriginalRecord")) &&
            FJsonSerializer::Deserialize(
                TJsonReaderFactory<TCHAR>::Create(CopyMeta->GetValue(Copy.Get(), TEXT("P4KadanCopiedGameAsset"))),
                History) &&
            History.IsValid() && History->TryGetObjectField(TEXT("source_records"), Records) &&
            P4ExactNativeString((*Records)->GetStringField(TEXT("P4OriginalRecord")), Marker) &&
            !CopyMeta->HasValue(Copy.Get(), TEXT("P4MaterialInstanceNativeSourceV1")) &&
            (!HadNativeSource ||
                ((*Records)->HasTypedField<EJson::String>(TEXT("P4MaterialInstanceNativeSourceV1")) &&
                    P4ExactNativeString(
                        (*Records)->GetStringField(TEXT("P4MaterialInstanceNativeSourceV1")), NativeSource))) &&
            EditableCopy());
    if (HadNativeSource && Records)
    {
        const FString CompleteHistory = CopyMeta->GetValue(Copy.Get(), TEXT("P4KadanCopiedGameAsset"));
        (*Records)->RemoveField(TEXT("P4MaterialInstanceNativeSourceV1"));
        FString Missing;
        FJsonSerializer::Serialize(History.ToSharedRef(), TJsonWriterFactory<TCHAR>::Create(&Missing));
        CopyMeta->SetValue(Copy.Get(), TEXT("P4KadanCopiedGameAsset"), *Missing);
        TestTrue(TEXT("A metadata-preserving fresh copy cannot silently lose its archived source custody"),
            Compare(TEXT("missing-archived-native-source")).State == EP4CopiedMICContent::Unreadable);
        CopyMeta->SetValue(Copy.Get(), TEXT("P4KadanCopiedGameAsset"), *CompleteHistory);
    }
    FString CopyError;
    TestTrue(TEXT("Copied data never obtains strict original-reimport comparison authority"),
        P4MaterialInstanceComparisonState(Copy.Get(), CopyError).IsEmpty() && !CopyError.IsEmpty());
    const FString CopyPath = Copy->GetPathName();
    Receipt->SetStringField(TEXT("copy_asset"), CopyPath);
    const auto Cold = [&]()
    {
        FString Why;
        if (!P4SaveAssetPackage(Copy.Get(), [&]() { return EditableCopy(); }, Why))
        {
            AddError(TEXT("Ordinary creator copy save failed: ") + Why);
            return false;
        }
        UPackage* Owned = Copy->GetOutermost();
        Copy.Reset();
        FText ReloadError;
        if (!UPackageTools::ReloadPackages({Owned}, ReloadError, EReloadPackagesInteractionMode::AssumePositive))
        {
            AddError(TEXT("Ordinary creator copy cold reload failed: ") + ReloadError.ToString());
            return false;
        }
        Copy.Reset(LoadObject<UMaterialInstanceConstant>(nullptr, *CopyPath));
        return Copy.IsValid() && EditableCopy() && SupportedParameters(Copy.Get());
    };
    if (!TestTrue(TEXT("Actual ordinary save/cold reload preserves editable copied origin"), Cold()))
        return false;
    TestFalse(TEXT("The unchanged cold copy remains refused by full Review"),
        P4ExportAssetAvailability(Copy.Get(), Mod).IsAvailable());
    Copy->SetScalarParameterValueEditorOnly(Scalar.ParameterInfo, Scalar.ParameterValue + 0.125f);
    if (!TestTrue(TEXT("The meaningful creator edit survives another ordinary save/cold reload"), Cold()))
        return false;
    const auto* Edited = Copy->ScalarParameterValues.FindByPredicate(
        [&](const auto& Value) { return Value.ParameterInfo == Scalar.ParameterInfo; });
    TestTrue(TEXT("The actual saved edit remains exportable without unlocking the game original"),
        Edited && Edited->ParameterValue == Scalar.ParameterValue + 0.125f &&
            P4ExportAssetAvailability(Copy.Get(), Mod).Route == EP4ExportAssetRoute::CreatorMaterialInstance &&
            !P4Kadan::GetAssetPolicy(Original.Get()).AllowEdit);
    Receipt->SetBoolField(TEXT("actual_duplicate_and_two_cold_cycles_completed"), true);
    return !HasAnyErrors();
}
#endif
