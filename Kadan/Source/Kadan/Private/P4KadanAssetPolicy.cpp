#include "P4KadanAssetPolicy.h"
#include "P4GameNames.h"
#include "P4NativeCustomCompilerBridge.h"
#include "Misc/App.h"
#include "P4OriginalRecord.h"
#include "P4CreatorSource.h"
#include "P4GameReplacement.h"
#include "P4KadanMaterialExpressions.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstance.h"
#include "P4MapKitCore/custom_material_support.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "Engine/Blueprint.h"
#include "P4MaterialInstanceGameSettings.h"
#include "Materials/MaterialInstanceConstant.h"
#include "GameFramework/Actor.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "IMaterialEditor.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialFunctionInterface.h"
#include "MaterialShared.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace P4Kadan
{
namespace
{
FDelegateHandle OpenedHandle, TickHandle;
TWeakObjectPtr<UAssetEditorSubsystem> BoundSubsystem;

struct FTabGuard
{
    TWeakPtr<SDockTab> Tab;
    TSharedPtr<SWidget> OriginalContent, Wrapper;
};
struct FCommandGuard
{
    TWeakPtr<FUICommandList> Commands;
    TSharedPtr<const FUICommandInfo> Command;
    FUIAction OriginalAction;
};
TArray<FTabGuard> Tabs;
TArray<FCommandGuard> Commands;
TMap<TWeakObjectPtr<UObject>, FAssetPolicy> UIReadbacks;

UAssetEditorSubsystem* Subsystem()
{
    return GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
}

void Issue(FAssetPolicy& Out, const FString& Code, const FString& Reason, const FString& Subject = FString())
{
    for (const FAssetPolicyIssue& Existing : Out.Issues)
        if (Existing.Code == Code && Existing.Subject == Subject)
            return;
    FAssetPolicyIssue Value;
    Value.Code = Code;
    Value.Reason = Reason;
    Value.Subject = Subject;
    Out.Issues.Add(MoveTemp(Value));
}

// Use the editor's unsaved preview copy so a new unsupported node appears before Apply.
UMaterial* CurrentMaterial(UMaterial* Original)
{
    UAssetEditorSubsystem* Editors = Subsystem();
    IAssetEditorInstance* Editor = Editors ? Editors->FindEditorForAsset(Original, false) : nullptr;
    if (Editor && Editor->GetEditorName() == FName(TEXT("MaterialEditor")))
        if (UMaterial* Preview = Cast<UMaterial>(static_cast<IMaterialEditor*>(Editor)->GetMaterialInterface()))
            return Preview;
    return Original;
}

void MaterialIssues(UMaterial* Material, FAssetPolicy& Out)
{
    using namespace P4MapKit::CustomMaterial;
    // Export compiles a creator material with UE's own material translator for the game (P4UseSwitchShaderRoute):
    // the graph, settings and nodes UE compiles are what it compiles. The checks below are those of Kadan's older
    // emitter, which that export no longer uses; they named settings and nodes the export takes as they are.
    if (P4UseSwitchShaderRoute())
        return;
    TArray<UMaterialExpression*> Pending = Material->Expressions;
    TSet<UMaterialExpression*> Visited;
    TSet<UMaterialFunctionInterface*> Functions;
    unsigned TextureCount = 0;
    for (int32 Index = 0; Index < Pending.Num(); ++Index)
    {
        UMaterialExpression* Expression = Pending[Index];
        if (!IsValid(Expression) || Visited.Contains(Expression))
            continue;
        Visited.Add(Expression);
        FString Type = Expression->GetClass()->GetName();
        Type.RemoveFromStart(TEXT("MaterialExpression"));
        const Availability Node = NodeAvailability(TCHAR_TO_UTF8(*Type));
        if (!Node.available)
            Issue(Out, UTF8_TO_TCHAR(Node.code.c_str()), UTF8_TO_TCHAR(Node.reason.c_str()), Type);
        if (Cast<UMaterialExpressionTextureSample>(Expression))
            ++TextureCount;
        if (UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
            if (Call->MaterialFunction && !Functions.Contains(Call->MaterialFunction))
            {
                Functions.Add(Call->MaterialFunction);
                if (const TArray<UMaterialExpression*>* Expressions = Call->MaterialFunction->GetFunctionExpressions())
                    Pending.Append(*Expressions);
            }
    }
    if (Material->MaterialDomain == MD_Surface &&
        (Material->GetBlendMode() == BLEND_Translucent || Material->GetBlendMode() == BLEND_Opaque ||
            Material->GetBlendMode() == BLEND_Masked))
    {
        const FString GraphIssue = P4CreatorMaterialEditorGraphIssue(Material);
        if (!GraphIssue.IsEmpty())
            Issue(Out, TEXT("translucent-graph-unavailable"), GraphIssue, TEXT("Material settings"));
        return;
    }
    RenderRequest Request; // Current graph and binding versions, production=true, lighting=game.
    switch (Material->MaterialDomain)
    {
        case MD_Surface:
            Request.domain = "Surface";
            break;
        case MD_DeferredDecal:
            Request.domain = "DeferredDecal";
            break;
        case MD_LightFunction:
            Request.domain = "LightFunction";
            break;
        case MD_PostProcess:
            Request.domain = "PostProcess";
            break;
        case MD_UI:
            Request.domain = "UI";
            break;
        default:
            Request.domain = "Unsupported";
            break;
    }
    switch (Material->GetBlendMode())
    {
        case BLEND_Opaque:
            Request.blend = "Opaque";
            break;
        case BLEND_Masked:
            Request.blend = "Masked";
            break;
        case BLEND_Translucent:
            Request.blend = "Translucent";
            break;
        case BLEND_Additive:
            Request.blend = "Additive";
            break;
        case BLEND_Modulate:
            Request.blend = "Modulate";
            break;
        case BLEND_AlphaComposite:
            Request.blend = "AlphaComposite";
            break;
        default:
            Request.blend = "Unsupported";
            break;
    }
    const FMaterialShadingModelField Models = Material->GetShadingModels();
    Request.shading = Models.HasOnlyShadingModel(MSM_Unlit) ? "Unlit"
        : Models.HasOnlyShadingModel(MSM_DefaultLit)        ? "DefaultLit"
                                                            : "Unsupported";
    Request.two_sided = Material->IsTwoSided();
    Request.tangent_space_normal = Material->bTangentSpaceNormal;
    Request.custom_texture_list = true;
    Request.texture_count = TextureCount;
    const Availability Render = RenderAvailability(Request);
    if (!Render.available)
        Issue(Out, UTF8_TO_TCHAR(Render.code.c_str()), UTF8_TO_TCHAR(Render.reason.c_str()), TEXT("Material settings"));
    // The graph can be placed on an instanced mesh later. Surface that distinct pass
    // limitation while editing a masked material rather than hiding it behind lighting.
    if (Request.blend == "Masked")
    {
        Request.mesh = "instanced-static";
        const Availability Instanced = RenderAvailability(Request);
        if (!Instanced.available && Instanced.code != Render.code)
            Issue(Out, UTF8_TO_TCHAR(Instanced.code.c_str()), UTF8_TO_TCHAR(Instanced.reason.c_str()),
                TEXT("Instanced meshes"));
    }
}

const FAssetPolicy& UIReadback(UObject* Asset)
{
    const TWeakObjectPtr<UObject> Key(Asset);
    if (const FAssetPolicy* Existing = UIReadbacks.Find(Key))
        return *Existing;
    return UIReadbacks.Add(Key, GetAssetPolicy(Asset));
}

UMaterial* EditedMaterial(const TWeakPtr<IMaterialEditor>& WeakEditor)
{
    const TSharedPtr<IMaterialEditor> Editor = WeakEditor.Pin();
    if (!Editor)
        return nullptr;
    // Apply reconstructs OriginalMaterial in place, invalidating its old weak
    // UObject identity. The owning toolkit supplies the current real asset.
    const TArray<UObject*>* Assets = Editor->GetObjectsCurrentlyBeingEdited();
    UMaterial* Edited = nullptr;
    if (Assets)
        for (UObject* Asset : *Assets)
        {
            UMaterial* Material = IsValid(Asset) ? Cast<UMaterial>(Asset) : nullptr;
            // Stock MaterialEditor also registers its transient UPreviewMaterial
            // and preview-parameters object. Neither is the edited source asset.
            if (!Material || !Material->IsAsset())
                continue;
            if (Edited)
                return nullptr;
            Edited = Material;
        }
    return Edited;
}

FText Banner(const TWeakPtr<IMaterialEditor>& Editor)
{
    const FAssetPolicy& Policy = UIReadback(EditedMaterial(Editor));
    FString Text = Policy.Reason;
    if (!Policy.PreviewNotice.IsEmpty())
        Text += (Text.IsEmpty() ? TEXT("") : TEXT(" ")) + Policy.PreviewNotice;
    for (const FAssetPolicyIssue& Problem : Policy.Issues)
    {
        if (!Text.IsEmpty())
            Text += TEXT("\n");
        if (!Problem.Subject.IsEmpty())
            Text += Problem.Subject + TEXT(". ");
        Text += Problem.Reason;
    }
    return FText::FromString(Text);
}

void GuardCommand(
    const TSharedRef<FUICommandList>& List, TSharedPtr<const FUICommandInfo> Command, TWeakPtr<IMaterialEditor> Editor)
{
    if (!Command)
        return;
    for (const FCommandGuard& Guard : Commands)
        if (Guard.Commands.Pin() == List && Guard.Command == Command)
            return;
    const FUIAction* Existing = List->GetActionForCommand(Command);
    if (!Existing)
        return;
    FCommandGuard Guard;
    Guard.Commands = List;
    Guard.Command = Command;
    Guard.OriginalAction = *Existing;
    FUIAction Filtered = *Existing;
    const FCanExecuteAction Previous = Existing->CanExecuteAction;
    Filtered.CanExecuteAction = FCanExecuteAction::CreateLambda(
        [Editor, Previous]()
        {
            UMaterial* Asset = EditedMaterial(Editor);
            return IsValid(Asset) && UIReadback(Asset).AllowEdit && (!Previous.IsBound() || Previous.Execute());
        });
    List->MapAction(Command, Filtered);
    Commands.Add(MoveTemp(Guard));
}

void GuardMaterial(UObject* Asset, IAssetEditorInstance* Editor)
{
    if (!Cast<UMaterial>(Asset) || !Editor || Editor->GetEditorName() != FName(TEXT("MaterialEditor")))
        return;
    IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(Editor);
    const TWeakPtr<IMaterialEditor> WeakEditor = StaticCastSharedRef<IMaterialEditor>(MaterialEditor->AsShared());
    const TSharedRef<FUICommandList> List = MaterialEditor->GetToolkitCommands();
    GuardCommand(List, FGenericCommands::Get().Undo, WeakEditor);
    GuardCommand(List, FGenericCommands::Get().Redo, WeakEditor);
    // FMaterialEditor::SaveAsset_Execute calls UpdateOriginalMaterial directly. Filtering
    // Apply alone would therefore leave a save/Save As bypass through the toolkit menu.
    for (const TCHAR* Name : {TEXT("SaveAsset"), TEXT("SaveAssetAs"), TEXT("ReimportAsset")})
        GuardCommand(List, FInputBindingManager::Get().FindCommandInContext(TEXT("AssetEditor"), Name), WeakEditor);
    // These names are UE4.26's exported FMaterialEditorCommands, not widget labels.
    for (const TCHAR* Name : {TEXT("Apply"), TEXT("Flatten"), TEXT("CleanUnusedExpressions"), TEXT("UseCurrentTexture"),
             TEXT("ConvertObjects"), TEXT("ConvertToTextureObjects"), TEXT("ConvertToTextureSamples"),
             TEXT("ConvertToConstant")})
        GuardCommand(List, FInputBindingManager::Get().FindCommandInContext(TEXT("MaterialEditor"), Name), WeakEditor);
    const TSharedPtr<FTabManager> Manager = Editor->GetAssociatedTabManager();
    if (!Manager)
        return;
    // Exact tab IDs from FMaterialEditor::RegisterTabSpawners in UE4.26. Its Preview and
    // PreviewSettings tabs and all preview commands stay available. No global widget walk.
    for (const TCHAR* Id : {TEXT("MaterialEditor_GraphCanvas"), TEXT("MaterialEditor_MaterialProperties"),
             TEXT("MaterialEditor_Palette"), TEXT("MaterialEditor_ParameterDefaults"),
             TEXT("MaterialEditor_CustomPrimitiveData"), TEXT("MaterialInstanceEditor_MaterialLayerProperties")})
    {
        TSharedPtr<SDockTab> Tab = Manager->FindExistingLiveTab(FTabId(FName(Id)));
        if (!Tab)
            continue;
        bool AlreadyWrapped = false;
        for (const FTabGuard& Guard : Tabs)
            if (Guard.Tab.Pin() == Tab)
            {
                AlreadyWrapped = true;
                break;
            }
        if (AlreadyWrapped)
            continue;
        FTabGuard Guard;
        Guard.Tab = Tab;
        Guard.OriginalContent = Tab->GetContent();
        Guard.Wrapper = SNew(SVerticalBox) +
            SVerticalBox::Slot().AutoHeight().Padding(
                8)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([WeakEditor]() { return Banner(WeakEditor); })] +
            SVerticalBox::Slot().FillHeight(1)[SNew(SBox).IsEnabled_Lambda(
                [WeakEditor]()
                {
                    UMaterial* Current = EditedMaterial(WeakEditor);
                    return IsValid(Current) && UIReadback(Current).AllowEdit;
                })[Guard.OriginalContent.ToSharedRef()]];
        Tab->SetContent(Guard.Wrapper.ToSharedRef());
        Tabs.Add(MoveTemp(Guard));
    }
}

bool Refresh(float)
{
    UAssetEditorSubsystem* Editors = BoundSubsystem.Get();
    if (!Editors)
    {
        Editors = Subsystem();
        if (Editors)
        {
            BoundSubsystem = Editors;
            OpenedHandle = Editors->OnAssetOpenedInEditor().AddLambda(
                [](UObject* Asset, IAssetEditorInstance* Editor) { GuardMaterial(Asset, Editor); });
        }
    }
    if (!Editors)
        return true;
    UIReadbacks.Reset();
    // Floating/restored tabs can be created after the editor-open notification.
    for (UObject* Asset : Editors->GetAllEditedAssets())
        if (Cast<UMaterial>(Asset))
        {
            UIReadback(Asset);
            for (IAssetEditorInstance* Editor : Editors->FindEditorsForAsset(Asset))
                GuardMaterial(Asset, Editor);
        }
    Tabs.RemoveAll([](const FTabGuard& Guard) { return !Guard.Tab.IsValid(); });
    Commands.RemoveAll([](const FCommandGuard& Guard) { return !Guard.Commands.IsValid(); });
    return true;
}
} //namespace

EGameAssetFamily GameAssetFamily(const UObject* Asset)
{
    if (!Asset)
        return EGameAssetFamily::Other;
    if (Asset->IsA<UWorld>())
        return EGameAssetFamily::Level;
    if (Asset->IsA<UDataTable>())
        return EGameAssetFamily::DataTable;
    if (const UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        if (Blueprint->GetClass()->GetFName() == TEXT("AnimBlueprint"))
            return EGameAssetFamily::AnimationBlueprint;
        if (Blueprint->GetClass()->GetFName() == TEXT("WidgetBlueprint"))
            return EGameAssetFamily::WidgetBlueprint;
        if (Blueprint->BlueprintType == BPTYPE_Interface)
            return EGameAssetFamily::BlueprintInterface;
        return Blueprint->GetClass() == UBlueprint::StaticClass() && Blueprint->BlueprintType == BPTYPE_Normal &&
                Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(AActor::StaticClass())
            ? EGameAssetFamily::ActorBlueprint
            : EGameAssetFamily::OtherBlueprint;
    }
    // Walk the actual class hierarchy so subclasses keep their family's gate.
    for (const UClass* Class = Asset->GetClass(); Class; Class = Class->GetSuperClass())
    {
        const FName Name = Class->GetFName();
        if (Name == TEXT("Texture"))
            return EGameAssetFamily::Texture;
        if (Name == TEXT("StaticMesh"))
            return EGameAssetFamily::StaticMesh;
        if (Name == TEXT("SkeletalMesh"))
            return EGameAssetFamily::SkeletalMesh;
        if (Name == TEXT("MaterialInstance"))
        {
            return EGameAssetFamily::MaterialInstance;
        }
        if (Name == TEXT("MaterialInterface") || Name == TEXT("MaterialFunctionInterface"))
            return EGameAssetFamily::Material;
        if (Name == TEXT("MaterialParameterCollection"))
            return EGameAssetFamily::MaterialParameters;
        if (Name == TEXT("AnimationAsset"))
            return EGameAssetFamily::Animation;
        if (Name == TEXT("Skeleton"))
            return EGameAssetFamily::Skeleton;
        if (Name == TEXT("PhysicsAsset"))
            return EGameAssetFamily::PhysicsAsset;
        if (Name == TEXT("ParticleSystem"))
            return EGameAssetFamily::Particle;
        if (Name == TEXT("NiagaraSystem") || Name == TEXT("NiagaraEmitter"))
            return EGameAssetFamily::Niagara;
        if (Name == TEXT("SoundBase"))
            return EGameAssetFamily::Sound;
        if (Name == TEXT("AkAudioEvent"))
            return EGameAssetFamily::AudioEvent;
        if (Name == TEXT("MovieSceneSequence"))
            return EGameAssetFamily::Sequence;
        if (Name == TEXT("CurveBase"))
            return EGameAssetFamily::Curve;
        if (Name == TEXT("Font") || Name == TEXT("FontFace"))
            return EGameAssetFamily::Font;
        if (Name == TEXT("DataAsset"))
            return EGameAssetFamily::DataAsset;
        if (Name == TEXT("UserDefinedStruct"))
            return EGameAssetFamily::UserStruct;
        if (Name == TEXT("UserDefinedEnum"))
            return EGameAssetFamily::UserEnum;
    }
    return EGameAssetFamily::Other;
}

bool IsRecoveredGameActorBlueprint(const UObject* Asset)
{
    if (GameAssetFamily(Asset) != EGameAssetFamily::ActorBlueprint)
        return false;
    FP4OriginalRecord Record;
    FString Error;
    if (!P4ReadOriginalRecord(const_cast<UObject*>(Asset), Record, Error) || Record.ReadOnly ||
        Record.Kind != TEXT("blueprint") ||
        (Record.Fidelity != TEXT("recovered") && Record.Fidelity != TEXT("recovered-unverified") &&
            Record.Fidelity != TEXT("exact")))
        return false;
    UMetaData* Meta = Asset->GetOutermost()->GetMetaData();
    return FString(Meta->GetValue(Asset, TEXT("P4BlueprintStandIn"))) != TEXT("1") && !P4HasIntrinsicGameProxy(Asset) &&
        FString(Meta->GetValue(Asset, TEXT("P4BlueprintReconstructionIncomplete"))) != TEXT("1");
}

bool IsRecoveredGameMaterialInstance(const UObject* Asset)
{
    if (!Asset || Asset->GetClass() != UMaterialInstanceConstant::StaticClass() ||
        !UP4MaterialInstanceGameSettings::Find(Cast<UMaterialInstanceConstant>(Asset)))
    {
        return false;
    }
    FP4OriginalRecord Record;
    FString Error;
    return P4ReadOriginalRecord(const_cast<UObject*>(Asset), Record, Error) &&
        Record.Kind == TEXT("material-instance") &&
        Record.ComparisonSchema == TEXT("material-instance-native-state-v1") &&
        Record.SourceDigestKind == TEXT("material-instance-v2-md5") && !P4HasIntrinsicGameProxy(Asset) &&
        FString(Asset->GetOutermost()->GetMetaData()->GetValue(Asset, TEXT("P4BlueprintStandIn"))) != TEXT("1");
}

FAssetPolicy GetAssetPolicy(UObject* Asset)
{
    FAssetPolicy Out;
    if (!IsInGameThread() || !IsValid(Asset))
    {
        Out.AllowEdit = false;
        Out.AllowPreview = false;
        Out.Reason = TEXT("Select an asset in the editor.");
        return Out;
    }
    UObject* Source = Asset;
    if (UClass* Class = Cast<UClass>(Asset))
        if (Class->ClassGeneratedBy)
            Source = Class->ClassGeneratedBy;
    if (Asset->HasAnyFlags(RF_ClassDefaultObject))
        if (Asset->GetClass()->ClassGeneratedBy)
            Source = Asset->GetClass()->ClassGeneratedBy;
    Out.StandIn = P4HasIntrinsicGameProxy(Source) ||
        FString(Source->GetOutermost()->GetMetaData()->GetValue(Source, TEXT("P4BlueprintStandIn"))) == TEXT("1");
    FP4OriginalRecord Record;
    FString Error;
    Out.Original = P4ReadOriginalRecord(Source, Record, Error);
    const UClass* NativeClass = Cast<UClass>(Asset);
    if (!NativeClass && Asset->HasAnyFlags(RF_ClassDefaultObject))
        NativeClass = Asset->GetClass();
    const bool GameClass = NativeClass && NativeClass->GetOutermost()->GetName() == TEXT(P4_GAME_SCRIPT_PACKAGE);
    Out.Original |= GameClass;
    Out.ReferenceOnly = Out.StandIn || (Out.Original && Record.ReferenceOnly);
    // "Replace with my own file" writes the game asset's package while it imports the creator's file.
    if (P4IsReplacingGameAsset(Source))
    {
        Out.AllowEdit = true;
        Out.ReferenceOnly = false;
        Out.StandIn = false;
        return Out;
    }
    const bool CreatorReplacement = P4HasCreatorSource(Source);
    FString IntrinsicPackage;
    if (CreatorReplacement)
    {
        Out.ReferenceOnly = false;
        if (P4IsGameReplacement(Source))
        {
            // The creator's file at the game asset's own path (K9). It is the creator's, and its record keeps
            // the game asset's history apart from the keys that say an asset is the game's.
            Out.Original = false;
            Out.Reason = TEXT(
                "This is your own file in place of the game's asset at its path. Export ships it, so everywhere the game uses this asset gets yours.");
        }
        else if (Out.Original || !Error.IsEmpty())
        {
            // A file imported over a game asset some other way still says it is the game's. Only "Replace with my
            // own file" records a replacement at the game's path.
            Out.AllowEdit = false;
            Out.Reason = TEXT(
                "Your file was imported over the game's asset. To ship it in the game asset's place, right-click it and choose Replace with my own file.");
        }
    }
    else if (Out.StandIn)
    {
        Out.AllowEdit = false;
        Out.Reason = TEXT("This is a reference-only stand-in. Its game data was not recovered.");
    }
    else if (!Out.Original && Error.IsEmpty() && P4HasIntrinsicGameSource(Source, &IntrinsicPackage) &&
        FName(*IntrinsicPackage) == Source->GetOutermost()->GetFName())
    {
        // A validated same-package game origin remains the game's when its
        // comparison record is absent. This does not recreate a record or baseline.
        Out.Original = true;
        Out.AllowEdit = false;
        Out.Reason = TEXT(
            "This is the game's own asset, but its original import record is missing. Reimport it from the game.");
    }
    else if (P4HasCopiedGameData(Source))
    {
        // A copy is the creator's (decided 2026-10-16). Review refuses it while it holds exactly the game's content.
        Out.AllowEdit = true;
        Out.ReferenceOnly = false;
        Out.Reason = TEXT(
            "This is your copy of a game asset. Change it to make it yours. Review refuses a copy that is still the same as the game's.");
    }
    else if (!Error.IsEmpty())
    {
        Out.AllowEdit = false;
        Out.Reason = TEXT("The asset's source information cannot be read. Reimport the asset.");
        UE_LOG(LogTemp, Warning, TEXT("Kadan source information %s: %s"), *Asset->GetPathName(), *Error);
    }
    else if (Out.Original &&
        (GameAssetShipping(GameAssetFamily(Source)) == EGameAssetShipping::Locked ||
            (Record.ReadOnly && !IsRecoveredGameMaterialInstance(Source)) ||
            (Record.ReferenceOnly && !IsRecoveredGameActorBlueprint(Source) &&
                !IsRecoveredGameMaterialInstance(Source)) ||
            (GameAssetFamily(Source) == EGameAssetFamily::MaterialInstance &&
                !IsRecoveredGameMaterialInstance(Source)) ||
            (GameAssetFamily(Source) == EGameAssetFamily::ActorBlueprint && !IsRecoveredGameActorBlueprint(Source))))
    {
        Out.AllowEdit = false;
        Out.Reason = Cast<UMaterial>(Asset)
            ? TEXT(
                  "Exporting edits to the game's material graph is not supported. Make a material instance or a new material in your mod folder.")
            : Record.ReferenceOnly
            ? TEXT(
                  "This game asset has data kept for reference. Exporting edits to it is not supported. Create supported content in your mod folder.")
            : (Asset->GetClass() == UTexture2D::StaticClass() || Asset->GetClass() == UStaticMesh::StaticClass())
            ? TEXT(
                  "This is an original game asset. Exporting edits to it is not supported. To ship your own in its place, right-click it and choose Replace with my own file.")
            : TEXT(
                  "This is an original game asset. Exporting edits to it is not supported. Import your own source file into your mod folder or create a supported asset there.");
    }
    else if (Out.Original && IsRecoveredGameActorBlueprint(Source))
    {
        Out.ReferenceOnly = false;
        Out.Reason = TEXT(
            "Export ships this edited Blueprint whole at the game's path, including its defaults, components and graphs.");
    }
    else if (Out.Original && IsRecoveredGameMaterialInstance(Source))
    {
        Out.ReferenceOnly = false;
        Out.Reason = TEXT("Export ships this material instance whole at the game's path, including parameters, "
                          "switches, base overrides and C4 settings.");
    }
    if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        if (Out.AllowEdit)
            MaterialIssues(CurrentMaterial(Material), Out);
    }
    Out.PreviewDiffers = PreviewDiffersFromGame(Asset);
    if (Out.PreviewDiffers)
        Out.PreviewNotice = PreviewNoticeText();
    // Browser/package guards consume this same policy. Stock asset actions route
    // protected objects to a separate reference editor; creator editors stay stock.
    return Out;
}

bool PreviewDiffersFromGame(UObject* Asset)
{
    const UMaterialInterface* Interface = Cast<UMaterialInterface>(Asset);
    const UMaterial* Base = Interface ? Interface->GetMaterial() : nullptr;
    if (!Base)
        return false;
    for (const UMaterialExpression* Expression : Base->Expressions)
        if (Expression &&
            (Expression->IsA<UMaterialExpressionKadanSceneDepth>() ||
                Expression->IsA<UMaterialExpressionKadanSceneColor>() ||
                Expression->IsA<UMaterialExpressionKadanGameCustom>()))
            return true;
    return false;
}

const TCHAR* PreviewNoticeText()
{
    return TEXT("The editor draws this material a little differently from the game. It looks right in the game.");
}

FText PreviewDescription(UObject* Asset, const FText& Description)
{
    if (!PreviewDiffersFromGame(Asset))
        return Description;
    const FString Line = TEXT("Preview differs from the game.");
    return FText::FromString(Description.IsEmpty() ? Line : Description.ToString() + TEXT("\n") + Line);
}

void InitializeAssetPolicy()
{
    if (TickHandle.IsValid() || IsRunningCommandlet() || !IsInGameThread() || !FApp::CanEverRender())
        return;
    // A module can load before GEditor. The ticker attaches once the subsystem exists.
    TickHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Refresh), 0.5f);
    Refresh(0);
}

void ShutdownAssetPolicy()
{
    if (UAssetEditorSubsystem* Editors = BoundSubsystem.Get())
        Editors->OnAssetOpenedInEditor().Remove(OpenedHandle);
    if (TickHandle.IsValid())
        FTicker::GetCoreTicker().RemoveTicker(TickHandle);
    for (const FTabGuard& Guard : Tabs)
        if (TSharedPtr<SDockTab> Tab = Guard.Tab.Pin())
            if (Tab->GetContent() == Guard.Wrapper)
                Tab->SetContent(Guard.OriginalContent.ToSharedRef());
    for (const FCommandGuard& Guard : Commands)
        if (TSharedPtr<FUICommandList> List = Guard.Commands.Pin())
            List->MapAction(Guard.Command, Guard.OriginalAction);
    Tabs.Reset();
    Commands.Reset();
    UIReadbacks.Reset();
    BoundSubsystem.Reset();
    OpenedHandle.Reset();
    TickHandle.Reset();
}
} //namespace P4Kadan
