// The JSON of a material's graph as it is in the editor.

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

namespace P4MaterialGraph
{
FString Json(UMaterial* Material, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || !IsValid(Material))
    {
        Error = TEXT("material-graph-needs-a-material-on-the-editor-thread");
        return FString();
    }
    TMap<const UMaterialExpression*, FString> Ids;
    TMap<FString, int32> Counts;
    for (const UMaterialExpression* Node : Material->Expressions)
    {
        if (!Node || Node->IsA<UMaterialExpressionClearCoatNormalCustomOutput>())
            continue;
        const FString Type = Node->GetClass()->GetName().RightChop(Prefix.Len());
        Ids.Add(Node, FString::Printf(TEXT("%s_%d"), *Type, Counts.FindOrAdd(Type)++));
    }
    const auto Link = [&](const FExpressionInput& In, TSharedPtr<FJsonValue>& Out) -> bool
    {
        const FString* Id = Ids.Find(In.Expression);
        if (!Id)
            return false;
        FString Channels;
        if (In.Mask)
            Channels = FString(In.MaskR ? TEXT("R") : TEXT("")) + (In.MaskG ? TEXT("G") : TEXT("")) +
                (In.MaskB ? TEXT("B") : TEXT("")) + (In.MaskA ? TEXT("A") : TEXT(""));
        TArray<TSharedPtr<FJsonValue>> Items{MakeShared<FJsonValueString>(*Id), MakeShared<FJsonValueString>(Channels)};
        if (In.OutputIndex != 0)
            Items.Add(MakeShared<FJsonValueNumber>(double(In.OutputIndex)));
        Out = MakeShared<FJsonValueArray>(Items);
        return true;
    };
    TMap<FString, TSharedPtr<FJsonObject>> Entries;
    for (UMaterialExpression* Node : Material->Expressions)
    {
        if (!Node || Node->IsA<UMaterialExpressionClearCoatNormalCustomOutput>())
            continue;
        const UObject* Defaults = Node->GetClass()->GetDefaultObject();
        TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>(), Inputs = MakeShared<FJsonObject>();
        if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Node))
        {
            TArray<TSharedPtr<FJsonValue>> Names;
            for (const FCustomInput& Input : Custom->Inputs)
            {
                TSharedRef<FJsonObject> Name = MakeShared<FJsonObject>();
                Name->SetStringField(TEXT("InputName"), Input.InputName.ToString());
                Names.Add(MakeShared<FJsonValueObject>(Name));
                if (Input.Input.Expression)
                {
                    TSharedPtr<FJsonValue> Value;
                    if (!Link(Input.Input, Value))
                    {
                        Error = TEXT("material-graph-custom-input-from-outside-the-material");
                        return FString();
                    }
                    Inputs->SetField(Input.InputName.ToString(), Value);
                }
            }
            Props->SetArrayField(TEXT("Inputs"), Names);
        }
        if (UMaterialExpressionKadanGameCustom* Game = Cast<UMaterialExpressionKadanGameCustom>(Node))
        {
            TArray<TSharedPtr<FJsonValue>> Names;
            for (const FKadanGameCustomInput& Input : Game->Inputs)
            {
                TSharedRef<FJsonObject> Name = MakeShared<FJsonObject>();
                Name->SetStringField(TEXT("InputName"), Input.InputName.ToString());
                Names.Add(MakeShared<FJsonValueObject>(Name));
                if (Input.Input.Expression)
                {
                    TSharedPtr<FJsonValue> Value;
                    if (!Link(Input.Input, Value))
                    {
                        Error = TEXT("material-graph-custom-input-from-outside-the-material");
                        return FString();
                    }
                    Inputs->SetField(Input.InputName.ToString(), Value);
                }
            }
            Props->SetArrayField(TEXT("Inputs"), Names);
        }
        for (TFieldIterator<FProperty> Field(Node->GetClass()); Field; ++Field)
        {
            if (Field->GetOwnerClass() == UMaterialExpression::StaticClass() ||
                Field->HasAnyPropertyFlags(CPF_Transient))
                continue;
            if (Field->GetName() == TEXT("Inputs") &&
                (Cast<UMaterialExpressionCustom>(Node) || Cast<UMaterialExpressionKadanGameCustom>(Node)))
                continue;
            const void* At = Field->ContainerPtrToValuePtr<void>(Node);
            const FStructProperty* Struct = CastField<FStructProperty>(*Field);
            if (Struct && IsInput(Struct->Struct))
            {
                const FExpressionInput& In = *static_cast<const FExpressionInput*>(At);
                TSharedPtr<FJsonValue> Value;
                if (!In.Expression)
                    continue;
                if (!Link(In, Value))
                {
                    Error = TEXT("material-graph-input-from-outside-the-material: ") + Ids[Node];
                    return FString();
                }
                Inputs->SetField(Field->GetName(), Value);
                continue;
            }
            // The ids the engine gives a parameter are not part of the graph.
            if (Struct && Struct->Struct == TBaseStructure<FGuid>::Get())
                continue;
            if (Field->Identical(At, Field->ContainerPtrToValuePtr<void>(Defaults)) &&
                !(Cast<UMaterialExpressionTextureSample>(Node) &&
                    (Field->GetName() == TEXT("SamplerType") || Field->GetName() == TEXT("SamplerSource") ||
                        Field->GetName() == TEXT("MipValueMode") || Field->GetName() == TEXT("bApplyViewMipBias"))))
                continue;
            TSharedPtr<FJsonValue> Value;
            if (!Get(*Field, At, Value))
            {
                Error = FString::Printf(TEXT("material-graph-cannot-say: %s.%s"), *Ids[Node], *Field->GetName());
                return FString();
            }
            Props->SetField(Field->GetName(), Value);
        }
        TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("type"), Node->GetClass()->GetName().RightChop(Prefix.Len()));
        Entry->SetObjectField(TEXT("inputs"), Inputs);
        Entry->SetObjectField(TEXT("props"), Props);
        Entries.Add(Ids[Node], Entry);
    }
    TSharedRef<FJsonObject> Nodes = MakeShared<FJsonObject>();
    TArray<FString> Sorted;
    Entries.GetKeys(Sorted);
    SortIds(Sorted);
    for (const FString& Id : Sorted)
        Nodes->SetObjectField(Id, Entries[Id]);
    TSharedRef<FJsonObject> Outputs = MakeShared<FJsonObject>();
    for (UMaterialExpression* Node : Material->Expressions)
        if (auto* Normal = Cast<UMaterialExpressionClearCoatNormalCustomOutput>(Node))
        {
            TSharedPtr<FJsonValue> Value;
            if (Normal->Input.Expression && Link(Normal->Input, Value))
                Outputs->SetField(TEXT("ClearCoatBottomNormal"), Value);
        }
    for (const TCHAR* Pin : Pins)
    {
        const FExpressionInput* In = InputOf(Material, Pin);
        TSharedPtr<FJsonValue> Value;
        if (!In || !In->Expression)
            continue;
        if (!Link(*In, Value))
        {
            Error = FString(TEXT("material-graph-input-from-outside-the-material: ")) + Pin;
            return FString();
        }
        Outputs->SetField(Pin, Value);
    }
    TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
    Graph->SetObjectField(TEXT("nodes"), Nodes);
    Graph->SetObjectField(TEXT("outputs"), Outputs);
    Graph->SetNumberField(TEXT("format"), 2);
    Graph->SetStringField(TEXT("domain"),
        StaticEnum<EMaterialDomain>()->GetNameStringByValue(int64(Material->MaterialDomain.GetValue())));
    const TCHAR* RenderNames[] = {TEXT("bFullyRough"), TEXT("bAllowNegativeEmissiveColor"),
        TEXT("TranslucencyLightingMode"), TEXT("bEnableSeparateTranslucency"),
        TEXT("bEnableMobileSeparateTranslucency"), TEXT("bUseTranslucencyVertexFog"), TEXT("bApplyCloudFogging"),
        TEXT("bEnableResponsiveAA"), TEXT("bScreenSpaceReflections"), TEXT("bContactShadows"),
        TEXT("bDisableDepthTest"), TEXT("bOutputTranslucentVelocity"), TEXT("RefractionDepthBias"),
        TEXT("TranslucencyDirectionalLightingIntensity"), TEXT("TranslucentShadowDensityScale"),
        TEXT("TranslucentSelfShadowDensityScale"), TEXT("TranslucentSelfShadowSecondDensityScale"),
        TEXT("TranslucentSelfShadowSecondOpacity"), TEXT("TranslucentBackscatteringExponent"),
        TEXT("TranslucentMultipleScatteringExtinction"), TEXT("TranslucentShadowStartOffset"),
        TEXT("BlendableLocation"), TEXT("BlendableOutputAlpha"), TEXT("BlendablePriority"), TEXT("bIsBlendable"),
        TEXT("DecalBlendMode")};
    TSharedRef<FJsonObject> Render = MakeShared<FJsonObject>();
    const FString StoredRender = Material->GetOutermost()->GetMetaData()->GetValue(
        Material, TEXT("P4MaterialRenderSettingsV2"));
    TSharedPtr<FJsonObject> SourceRender;
    const bool RenderAbsent = StoredRender == TEXT("absent");
    if (!RenderAbsent && !StoredRender.IsEmpty() && (!P4ParseJson(StoredRender, SourceRender)))
    {
        Error = TEXT("material-graph-render-settings-record-unreadable");
        return FString();
    }
    const TSharedPtr<FJsonObject> KeptGameValues = GameRenderValues(Material);
    TArray<FString> ReadRenderNames;
    if (SourceRender.IsValid())
        SourceRender->Values.GetKeys(ReadRenderNames);
    else
        for (const TCHAR* Name : RenderNames)
            ReadRenderNames.Add(Name);
    ReadRenderNames.Sort();
    for (const FString& Name : ReadRenderNames)
    {
        if (RenderAbsent)
            continue;
        if (SourceRender.IsValid() && !SourceRender->HasField(Name))
            continue;
        UObject* Owner = nullptr;
        const FProperty* Property = RenderSettingProperty(Material, Name, false, Owner);
        TSharedPtr<FJsonValue> Value;
        if (Property && Get(Property, Property->ContainerPtrToValuePtr<void>(Owner), Value))
        {
            if (SourceRender.IsValid() && SourceRender->HasTypedField<EJson::Object>(Name) &&
                Value->Type == EJson::Array && Value->AsArray().Num() == 4)
            {
                TSharedRef<FJsonObject> Color = MakeShared<FJsonObject>();
                const TCHAR* Channels[] = {TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A")};
                for (int32 Index = 0; Index < 4; ++Index)
                    Color->SetField(Channels[Index], Value->AsArray()[Index]);
                Value = MakeShared<FJsonValueObject>(Color);
            }
            // A stand-in reads back as the game's value only while it is still exactly the stand-in.
            const TSharedPtr<FJsonObject>* Kept = nullptr;
            double Highest = -1;
            if (KeptGameValues.IsValid() && KeptGameValues->TryGetObjectField(Name, Kept) &&
                (*Kept)->TryGetNumberField(TEXT("highest_texcoord"), Highest))
            {
                const TSharedPtr<FJsonValue> GameValue = (*Kept)->TryGetField(TEXT("game")),
                                             EditorValue = (*Kept)->TryGetField(TEXT("editor"));
                const TSharedPtr<FJsonValue> Expected = GameRenderStandIn(Name, GameValue, int32(Highest));
                if (GameValue.IsValid() && EditorValue.IsValid() && Expected.IsValid() &&
                    P4CondensedJson(Expected) == P4CondensedJson(EditorValue) &&
                    P4CondensedJson(Value) == P4CondensedJson(EditorValue))
                    Value = GameValue;
            }
            Render->SetField(Name, Value);
        }
        else if (SourceRender.IsValid())
        {
            Error = TEXT("material-graph-render-setting-cannot-read: ") + Name;
            return FString();
        }
    }
    if (!RenderAbsent)
        Graph->SetObjectField(TEXT("render_settings"), Render);
    Graph->SetStringField(
        TEXT("blend_mode"), StaticEnum<EBlendMode>()->GetNameStringByValue(int64(Material->BlendMode.GetValue())));
    Graph->SetStringField(TEXT("shading_model"),
        StaticEnum<EMaterialShadingModel>()->GetNameStringByValue(
            int64(Material->GetShadingModels().GetFirstShadingModel())));
    Graph->SetBoolField(TEXT("two_sided"), Material->TwoSided != 0);
    Graph->SetBoolField(TEXT("tangent_space_normal"), Material->bTangentSpaceNormal != 0);
    Graph->SetNumberField(TEXT("clip"), double(Material->OpacityMaskClipValue));
    FString Out = P4CondensedJson(Graph);
    return Out;
}
} // namespace P4MaterialGraph

FString P4MaterialGraphJson(UMaterial* Material, FString& Error)
{
    return P4MaterialGraph::Json(Material, Error);
}
