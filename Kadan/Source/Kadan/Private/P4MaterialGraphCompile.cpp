// A material's native comparison state, waiting for its shaders, and the build report.

#include "P4MaterialGraphInternal.h"
#include "P4KadanReimportAsset.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4MaterialGraph.h"
#include "P4PackageSave.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "P4KadanMaterialExpressions.h"
#include "P4OriginalRawNormalOperation.h"
#include "P4OriginalPositiveInfinityOperation.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionClearCoatNormalCustomOutput.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "MaterialExpressionIO.h"
#include "MaterialShared.h"
#include "ShaderCompiler.h"
#include "RHI.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/MetaData.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/App.h"
#include "P4MaterialGameSettings.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/Notifications/SNotificationList.h"

static FString P4MaterialComparisonStateImpl(UMaterial* Material, FString& Error, FP4MaterialNativeStateFrame* Frame)
{
    if (Frame)
        *Frame = FP4MaterialNativeStateFrame();
    FString Graph = P4MaterialGraphJson(Material, Error);
    if (Graph.IsEmpty())
        return FString();
    // Source-shaped JSON deliberately omits inapplicable settings. Equality
    // must also see native edits to those omitted fields, with exact numeric
    // bits rather than human-readable ExportText rounding.
    class FBoundedWriter final : public FMemoryWriter
    {
    public:
        explicit FBoundedWriter(TArray<uint8>& OutBytes) : FMemoryWriter(OutBytes, true) {}
        virtual void Serialize(void* Data, int64 Size) override
        {
            constexpr int64 Limit = int64(64) * 1024 * 1024;
            if (IsError() || Size < 0 || Tell() < 0 || Tell() > Limit || Size > Limit - Tell())
            {
                SetError();
                return;
            }
            FMemoryWriter::Serialize(Data, Size);
        }
    };
    TArray<uint8> Bytes;
    FBoundedWriter Writer(Bytes);
    FObjectAndNameAsStringProxyArchive Archive(Writer, false);
    Archive.ArNoDelta = true;
    bool Complete = true;
    const auto Observe = [&](const FString& Object, const FString& Property, int64 Begin)
    {
        if (!Frame)
            return;
        if (Frame->Fields.Num() >= 8192)
        {
            Complete = false;
            return;
        }
        FP4MaterialNativeFieldRange Range;
        Range.Object = Object;
        Range.Property = Property;
        Range.Begin = Begin;
        Range.End = Writer.Tell();
        Frame->Fields.Add(MoveTemp(Range));
    };
    FString Schema = TEXT("p4-material-native-reflected-state/1");
    Archive << Schema;
    Archive << Graph;
    if (Frame)
        Observe(Material->GetPathName(), TEXT("$schema-and-graph"), 0);
    const auto Snapshot = [&](UObject* Owner)
    {
        if (!IsValid(Owner))
        {
            Archive.SetError();
            return;
        }
        const int64 HeaderBegin = Writer.Tell();
        FString Class = Owner->GetClass()->GetPathName(), Path = Owner->GetPathName();
        Archive << Class;
        Archive << Path;
        TArray<FProperty*> Fields;
        for (TFieldIterator<FProperty> Field(Owner->GetClass()); Field; ++Field)
            if (!Field->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization | CPF_Deprecated))
                Fields.Add(*Field);
        Fields.Sort([](const FProperty& A, const FProperty& B) { return A.GetPathName() < B.GetPathName(); });
        int32 Count = Fields.Num();
        Archive << Count;
        Observe(Path, TEXT("$class-path-field-count"), HeaderBegin);
        for (FProperty* Field : Fields)
        {
            const int64 Begin = Writer.Tell();
            FString Name = Field->GetPathName(), Type = Field->GetCPPType();
            int32 Dim = Field->ArrayDim;
            Archive << Name;
            Archive << Type;
            Archive << Dim;
            if (Dim <= 0 || Dim > 65536)
            {
                Archive.SetError();
                return;
            }
            for (int32 Index = 0; Index < Dim && !Archive.IsError(); ++Index)
                Field->SerializeItem(FStructuredArchiveFromArchive(Archive).GetSlot(),
                    Field->ContainerPtrToValuePtr<void>(Owner, Index), nullptr);
            if (Archive.IsError())
                return;
            Observe(Path, Name, Begin);
        }
    };
    Snapshot(Material);
    const int64 CountBegin = Writer.Tell();
    int32 Count = Material->Expressions.Num();
    Archive << Count;
    if (Frame)
        Observe(Material->GetPathName(), TEXT("$expression-count"), CountBegin);
    for (UMaterialExpression* Expression : Material->Expressions)
    {
        Snapshot(Expression);
        if (Archive.IsError())
            break;
    }
    if (Archive.IsError() || Writer.IsError())
    {
        Error = TEXT("material-native-comparison-serialization-failed");
        return FString();
    }
    FString State;
    if (!P4HashByteStream(
            [&](const TFunction<bool(const uint8*, int32)>& Update)
            {
                for (int32 At = 0; At < Bytes.Num(); At += 65536)
                    if (!Update(Bytes.GetData() + At, FMath::Min(65536, Bytes.Num() - At)))
                        return false;
                return true;
            },
            State))
    {
        Error = TEXT("material-native-comparison-hash-failed");
        return FString();
    }
    if (Frame)
    {
        Frame->Graph = MoveTemp(Graph);
        Frame->Bytes = MoveTemp(Bytes);
        Frame->Complete = Complete;
    }
    return State;
}
FString P4MaterialComparisonState(UMaterial* Material, FString& Error)
{
    return P4MaterialComparisonStateImpl(Material, Error, nullptr);
}
FString P4MaterialComparisonStateAndFrame(UMaterial* Material, FP4MaterialNativeStateFrame& Frame, FString& Error)
{
    return P4MaterialComparisonStateImpl(Material, Error, &Frame);
}
namespace
{
// The game masters an import saved with their shaders still compiling, and the one handler that tells
// about a graph that does not compile once its shaders are done.
TSet<FSoftObjectPath> CompilingMasters;
FDelegateHandle CompileFinished;
} //namespace
static void P4WatchMasterCompile(UMaterial* Material)
{
    CompilingMasters.Add(FSoftObjectPath(Material));
    if (CompileFinished.IsValid())
        return;
    CompileFinished = UMaterial::OnMaterialCompilationFinished().AddLambda(
        [](UMaterialInterface* Done)
        {
            UMaterial* Master = Cast<UMaterial>(Done);
            if (!Master || !CompilingMasters.Contains(FSoftObjectPath(Master)))
                return;
            const FMaterialResource* Resource = Master->GetMaterialResource(GMaxRHIFeatureLevel);
            if (!Resource || !Resource->IsCompilationFinished())
                return;
            CompilingMasters.Remove(FSoftObjectPath(Master));
            if (Resource->GetCompileErrors().Num() == 0 && Resource->HasValidGameThreadShaderMap())
                return;
            const FString Message = FString::Printf(
                TEXT("This editor cannot compile the rebuilt graph of the game material %s, so the editor ") TEXT(
                    "draws it and its instances with Unreal's default material. It still looks right in the game."),
                *Master->GetName());
            UE_LOG(LogTemp, Warning, TEXT("Kadan: %s %s"), *Message,
                *FString::Join(Resource->GetCompileErrors(), TEXT(" ")));
            if (FSlateApplication::IsInitialized() && FApp::CanEverRender())
            {
                FNotificationInfo Info(FText::FromString(Message));
                Info.ExpireDuration = 8.f;
                Info.bUseLargeFont = false;
                Info.WidthOverride = 480.f;
                FSlateNotificationManager::Get().AddNotification(Info);
            }
        });
}
bool P4WaitForMaterialShaders(UMaterial* Material, TArray<FString>& CompileErrors, FString& Error)
{
    CompileErrors.Reset();
    Error.Reset();
    if (!IsInGameThread() || !IsValid(Material) || GUsingNullRHI || !GShaderCompilingManager)
    {
        Error = TEXT("material-graph-real-renderer-compile-required");
        return false;
    }
    GShaderCompilingManager->FinishAllCompilation();
    FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIFeatureLevel);
    if (Resource)
        CompileErrors = Resource->GetCompileErrors();
    const bool Compiled = Resource && CompileErrors.Num() == 0 && Resource->HasValidGameThreadShaderMap();
    if (!Compiled)
        Error = TEXT("material-graph-renderer-compile-failed");
    return Compiled;
}
void P4MaterialSaveReferences(UMaterial* Material, TArray<UObject*>& Out)
{
    const auto Collect = [&](UObject* Owner)
    {
        for (TFieldIterator<FObjectProperty> Field(Owner->GetClass()); Field; ++Field)
        {
            UObject* Value = Field->GetObjectPropertyValue(Field->ContainerPtrToValuePtr<void>(Owner));
            if (Value && Value->GetOutermost() != Material->GetOutermost())
                Out.AddUnique(Value);
        }
    };
    Collect(Material);
    for (UMaterialExpression* Node : Material->Expressions)
        if (Node)
            Collect(Node);
}

FString P4BuildMaterialGraphReport(const FString& GraphJson, const FString& Package, bool SaveToDisk,
    const TFunction<bool(FString&)>& SourceCurrent, bool bWaitForShaders)
{
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    FString Error, Graph;
    Report->SetBoolField(TEXT("saved"), false);
    const auto CheckSource = [&]()
    {
        FString Why;
        if (SourceCurrent && !SourceCurrent(Why))
        {
            Error = TEXT("material-source-current-refused: ") + Why;
            return false;
        }
        return true;
    };
    TStrongObjectPtr<UMaterial> Material(CheckSource() ? P4BuildMaterialFromGraph(GraphJson, Package, Error) : nullptr);
    if (Material.IsValid())
    {
        Report->SetStringField(TEXT("asset"), Material->GetPathName());
        Report->SetNumberField(TEXT("nodes"), Material->Expressions.Num());
        Graph = P4MaterialGraphJson(Material.Get(), Error);
        TSharedPtr<FJsonObject> Read;
        if (!Graph.IsEmpty() && P4ParseJson(Graph, Read))
            Report->SetObjectField(TEXT("graph"), Read);
        Report->SetBoolField(TEXT("renderer_active"), !GUsingNullRHI);
        // PostEditChange has queued the shader map; an import does not wait for it.
        if (!GUsingNullRHI && Error.IsEmpty() && !bWaitForShaders)
            Report->SetBoolField(TEXT("compile_deferred"), true);
        else if (!GUsingNullRHI && Error.IsEmpty())
        {
            // PostEditChange queues the real platform shader map. Reuse the
            // same current-resource check as unsaved candidate qualification.
            TArray<FString> CompileErrors;
            const bool Compiled = P4WaitForMaterialShaders(Material.Get(), CompileErrors, Error);
            TArray<TSharedPtr<FJsonValue>> Errors;
            for (const FString& CompileError : CompileErrors)
                Errors.Add(MakeShared<FJsonValueString>(CompileError));
            Report->SetArrayField(TEXT("compile_errors"), Errors);
            Report->SetBoolField(TEXT("compiled"), Compiled);
        }
    }
    if (Material.IsValid() && Error.IsEmpty())
        CheckSource();
    if (Material.IsValid() && Error.IsEmpty() && SaveToDisk)
    {
        FString Unused, Why;
        if (P4SaveAssetPackage(
                Material.Get(),
                [&]()
                {
                    return CheckSource() &&
                        P4MaterialGraphJson(Material.Get(), Unused).Equals(Graph, ESearchCase::CaseSensitive);
                },
                Why))
        {
            Report->SetBoolField(TEXT("saved"), true);
            if (Report->HasField(TEXT("compile_deferred")))
                P4WatchMasterCompile(Material.Get());
        }
        else if (Error.IsEmpty())
            Error = TEXT("material-graph-save-refused: ") + Why;
    }
    if (Material.IsValid() && !Error.IsEmpty() && !Report->GetBoolField(TEXT("saved")))
    {
        // A refused graph must not leave an apparently imported public asset in this session.
        FAssetRegistryModule::AssetDeleted(Material.Get());
        Material->ClearFlags(RF_Public | RF_Standalone);
        Material->Rename(
            nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
        Material->MarkPendingKill();
    }
    Report->SetBoolField(TEXT("valid"), Material.IsValid() && Error.IsEmpty());
    Report->SetStringField(TEXT("error"), Error);
    FString Out = P4CondensedJson(Report);
    return Out;
}
