#include "P4MaterialCandidateSourceProof.h"
#include "P4Common.h"
#include "Dom/JsonObject.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "MaterialExpressionIO.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

// Same-module typed JSON/property codec. The proof does not call the graph
// builder or recreate its connections to manufacture its expected readback.
#include "P4MaterialGraphInternal.h"
namespace P4MaterialCandidateProof
{
bool Fail(FString& Error, const FString& Why)
{
    if (Error.IsEmpty())
        Error = TEXT("material-candidate-source-") + Why;
    return false;
}

bool Properties(UObject* Actual, UObject* Expected, const TSharedPtr<FJsonObject>& Props, FString& Error)
{
    for (const auto& Entry : Props->Values)
    {
        FProperty* Field = FindFProperty<FProperty>(Expected->GetClass(), *Entry.Key);
        if (!Field || Field->GetOwnerClass() == UMaterialExpression::StaticClass() ||
            !P4MaterialGraph::Set(Expected, Field, Entry.Value, Error))
            return Fail(Error, TEXT("property-cannot-be-interpreted:") + Entry.Key);
    }
    for (TFieldIterator<FProperty> It(Expected->GetClass()); It; ++It)
    {
        const FProperty* Field = *It;
        if (Field->GetOwnerClass() == UMaterialExpression::StaticClass() || Field->HasAnyPropertyFlags(CPF_Transient))
            continue;
        const auto* Struct = CastField<FStructProperty>(Field);
        if (Struct && (Struct->Struct == TBaseStructure<FGuid>::Get() || P4MaterialGraph::IsInput(Struct->Struct)))
            continue;
        if (!Field->Identical(
                Field->ContainerPtrToValuePtr<void>(Actual), Field->ContainerPtrToValuePtr<void>(Expected)))
            return Fail(Error, TEXT("property-differs:") + Field->GetName());
    }
    return true;
}

struct FValue
{
    FString Node, Channels;
};
bool Equal(const FValue& A, const FValue& B)
{
    return A.Node == B.Node && A.Channels == B.Channels;
}
bool Mask(FValue& Value, const FString& Channels, FString& Error)
{
    if (Channels.IsEmpty())
        return true;
    if (Channels.Len() > 4 || Channels.Contains(TEXT("|")))
        return Fail(Error, TEXT("invalid-channel-mask"));
    FString Selected;
    for (const TCHAR Channel : Channels)
    {
        const int32 At = FString(TEXT("RGBA")).Find(FString(1, &Channel));
        if (At == INDEX_NONE)
            return Fail(Error, TEXT("invalid-channel-mask"));
        if (Value.Channels.IsEmpty())
            Selected.AppendChar(Channel);
        else if (Value.Channels.IsValidIndex(At))
            Selected.AppendChar(Value.Channels[At]);
        else
            return Fail(Error, TEXT("channel-mask-outside-value"));
    }
    Value.Channels = Selected;
    return true;
}
FString Channels(const FExpressionInput& Input)
{
    if (!Input.Mask)
        return FString();
    return FString(Input.MaskR ? TEXT("R") : TEXT("")) + (Input.MaskG ? TEXT("G") : TEXT("")) +
        (Input.MaskB ? TEXT("B") : TEXT("")) + (Input.MaskA ? TEXT("A") : TEXT(""));
}
bool AllChannels(UMaterialExpression* Node)
{
    bool Color = false, Alpha = false;
    for (const FExpressionOutput& Pin : Node->GetOutputs())
    {
        if (!Pin.Mask)
            continue;
        if (Pin.MaskR && Pin.MaskG && Pin.MaskB && !Pin.MaskA)
            Color = true;
        if (!Pin.MaskR && !Pin.MaskG && !Pin.MaskB && Pin.MaskA)
            Alpha = true;
        if (Pin.MaskR && Pin.MaskG && Pin.MaskB && Pin.MaskA)
            return true;
    }
    return Color && Alpha;
}
struct FConnections
{
    TMap<UMaterialExpression*, FString> Names;
    TMap<FString, UMaterialExpression*> Nodes;
    TSet<UMaterialExpression*> Helpers;
    FString& Error;

    bool Source(const TSharedPtr<FJsonValue>& Link, FValue& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        FString Name, Requested;
        if (!Link.IsValid() || !Link->TryGetArray(Items) || Items->Num() != 2 || !(*Items)[0]->TryGetString(Name) ||
            !(*Items)[1]->TryGetString(Requested) || !Nodes.Contains(Name))
            return Fail(Error, TEXT("source-link-not-supported"));
        const auto& NativeOutputs = Nodes[Name]->GetOutputs();
        if (NativeOutputs.Num() == 0)
            return Fail(Error, TEXT("source-node-has-no-output"));
        const auto& First = NativeOutputs[0];
        FString Whole;
        if (First.Mask)
            Whole = AllChannels(Nodes[Name])
                ? TEXT("RGBA")
                : FString(First.MaskR ? TEXT("R") : TEXT("")) + (First.MaskG ? TEXT("G") : TEXT("")) +
                    (First.MaskB ? TEXT("B") : TEXT("")) + (First.MaskA ? TEXT("A") : TEXT(""));
        Out = {Name, Whole};
        return Mask(Out, Requested, Error);
    }
    bool Native(const FExpressionInput& Input, FValue& Out, int32 Depth = 0)
    {
        UMaterialExpression* Expression = Input.Expression;
        if (!Expression || Depth > 32)
            return Fail(Error, TEXT("native-link-missing-or-cyclic"));
        if (const FString* Name = Names.Find(Expression))
        {
            if (!Expression->GetOutputs().IsValidIndex(Input.OutputIndex))
                return Fail(Error, TEXT("native-output-index"));
            const auto& Outputs = Expression->GetOutputs();
            const auto& Declared = Outputs[Input.OutputIndex];
            const auto SameMask = [](const FExpressionOutput& Pin, const FExpressionInput& In)
            {
                return bool(Pin.Mask) == bool(In.Mask) &&
                    (!In.Mask ||
                        (bool(Pin.MaskR) == bool(In.MaskR) && bool(Pin.MaskG) == bool(In.MaskG) &&
                            bool(Pin.MaskB) == bool(In.MaskB) && bool(Pin.MaskA) == bool(In.MaskA)));
            };
            if (!SameMask(Declared, Input))
                return Fail(Error, TEXT("native-output-index-mask-disagrees"));
            int32 Matching = 0;
            for (const auto& Pin : Outputs)
                if (SameMask(Pin, Input))
                    ++Matching;
            if (Matching != 1)
                return Fail(Error, TEXT("native-output-port-ambiguous"));
            // FExpressionInput::Compile forwards OutputIndex before applying
            // its mask. CallExpression discards it for the native virtual
            // contract (TextureSample). Exact VectorParameter does not opt in
            // to that cache optimization, but its installed Compile implementation
            // returns the same float4 in both branches without reading OutputIndex.
            // Custom uses OutputIndex; never collapse its independent outputs.
            const bool IndexIgnored = Expression->CanIgnoreOutputIndex() ||
                Expression->GetClass() == UMaterialExpressionVectorParameter::StaticClass();
            if (Input.OutputIndex != 0 && !IndexIgnored)
                return Fail(Error, TEXT("native-secondary-output-has-independent-semantics"));
            Out = {*Name, Channels(Input)};
            // Nonzero unmasked outputs carry independent semantics. These two
            // source families never request them; don't alias one to output0.
            if (Input.OutputIndex && !Input.Mask)
                return Fail(Error, TEXT("unmasked-secondary-output"));
            return true;
        }
        Helpers.Add(Expression);
        if (Input.OutputIndex != 0)
            return Fail(Error, TEXT("adapter-secondary-output"));
        if (const auto* Component = Cast<UMaterialExpressionComponentMask>(Expression))
        {
            if (!Native(Component->Input, Out, Depth + 1))
                return false;
            const FString Pick = FString(Component->R ? TEXT("R") : TEXT("")) + (Component->G ? TEXT("G") : TEXT("")) +
                (Component->B ? TEXT("B") : TEXT("")) + (Component->A ? TEXT("A") : TEXT(""));
            return !Pick.IsEmpty() && Mask(Out, Pick, Error) && Mask(Out, Channels(Input), Error);
        }
        if (const auto* Append = Cast<UMaterialExpressionAppendVector>(Expression))
        {
            FValue A, B;
            if (!Native(Append->A, A, Depth + 1) || !Native(Append->B, B, Depth + 1))
                return false;
            if (A.Node != B.Node || A.Channels.IsEmpty() || B.Channels.IsEmpty() ||
                A.Channels.Len() + B.Channels.Len() > 4)
                return Fail(Error, TEXT("adapter-is-not-one-source-value"));
            Out = {A.Node, A.Channels + B.Channels};
            return Mask(Out, Channels(Input), Error);
        }
        return Fail(Error, TEXT("unexpected-adapter-expression"));
    }
    bool Match(const FExpressionInput* Actual, const TSharedPtr<FJsonValue>& Expected, const FString& Where)
    {
        FValue A, B;
        if (!Actual || !Source(Expected, B) || !Native(*Actual, A))
            return Fail(Error, TEXT("connection-unreadable:") + Where);
        return Equal(A, B) ||
            Fail(Error,
                TEXT("connection-differs:") + Where + TEXT(" actual=") + A.Node + TEXT(".") + A.Channels +
                    TEXT(" expected=") + B.Node + TEXT(".") + B.Channels);
    }
};
} //namespace P4MaterialCandidateProof

bool P4FreshMaterialCandidateMatchesSource(UMaterial* Candidate, const FString& SourceGraph, FString& Error)
{
    using namespace P4MaterialCandidateProof;
    Error.Reset();
    TSharedPtr<FJsonObject> Source;
    const TSharedPtr<FJsonObject>* Nodes = nullptr;
    const TSharedPtr<FJsonObject>* Outputs = nullptr;
    if (!IsValid(Candidate) || !P4ParseJson(SourceGraph, Source) || !Source->TryGetObjectField(TEXT("nodes"), Nodes) ||
        !Source->TryGetObjectField(TEXT("outputs"), Outputs) || (*Nodes)->Values.Num() == 0 ||
        (*Nodes)->Values.Num() > 4096 || Candidate->Expressions.Num() < (*Nodes)->Values.Num())
        return Fail(Error, TEXT("graph-unreadable"));
    TArray<FString> Ids;
    (*Nodes)->Values.GetKeys(Ids);
    P4MaterialGraph::SortIds(Ids);
    FConnections Connections{{}, {}, {}, Error};
    for (int32 Index = 0; Index < Ids.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* Node = nullptr;
        FString Type;
        if (!(*Nodes)->Values[Ids[Index]]->TryGetObject(Node) || !(*Node)->TryGetStringField(TEXT("type"), Type))
            return Fail(Error, TEXT("source-node-unreadable"));
        UMaterialExpression* Actual = Candidate->Expressions[Index];
        if (!IsValid(Actual) || Actual->GetOuter() != Candidate ||
            Actual->GetClass()->GetName() != TEXT("MaterialExpression") + Type)
            return Fail(Error, TEXT("node-class-or-construction-order:") + Ids[Index]);
        Connections.Names.Add(Actual, Ids[Index]);
        Connections.Nodes.Add(Ids[Index], Actual);
        TStrongObjectPtr<UMaterialExpression> Expected(
            NewObject<UMaterialExpression>(GetTransientPackage(), Actual->GetClass(), NAME_None, RF_Transient));
        if (auto* Custom = Cast<UMaterialExpressionCustom>(Expected.Get()))
            Custom->Inputs.Reset();
        const TSharedPtr<FJsonObject>* Props = nullptr;
        if (!(*Node)->TryGetObjectField(TEXT("props"), Props) || !Properties(Actual, Expected.Get(), *Props, Error))
            return false;
    }
    for (const FString& Id : Ids)
    {
        const auto Node = (*Nodes)->Values[Id]->AsObject();
        const TSharedPtr<FJsonObject>* Inputs = nullptr;
        if (!Node->TryGetObjectField(TEXT("inputs"), Inputs))
            return Fail(Error, TEXT("source-inputs-unreadable"));
        UMaterialExpression* Actual = Connections.Nodes[Id];
        for (const auto& Input : (*Inputs)->Values)
            if (!Connections.Match(
                    P4MaterialGraph::InputOf(Actual, Input.Key), Input.Value, Id + TEXT(".") + Input.Key))
                return false;
        for (TFieldIterator<FStructProperty> It(Actual->GetClass()); It; ++It)
            if (P4MaterialGraph::IsInput(It->Struct) &&
                It->ContainerPtrToValuePtr<FExpressionInput>(Actual)->Expression && !(*Inputs)->HasField(It->GetName()))
                return Fail(Error, TEXT("extra-connected-input:") + Id + TEXT(".") + It->GetName());
    }
    TMap<FString, uint8> State;
    TFunction<bool(const FString&, int32)> Visit = [&](const FString& Id, int32 Depth)
    {
        if (Depth > 512 || State.FindRef(Id) == 1)
            return Fail(Error, TEXT("source-input-cycle-or-depth"));
        if (State.FindRef(Id) == 2)
            return true;
        State.Add(Id, 1);
        const auto Inputs = (*Nodes)->Values[Id]->AsObject()->GetObjectField(TEXT("inputs"));
        for (const auto& Input : Inputs->Values)
        {
            FValue Target;
            if (!Connections.Source(Input.Value, Target) || !Visit(Target.Node, Depth + 1))
                return false;
        }
        State[Id] = 2;
        return true;
    };
    for (const FString& Id : Ids)
        if (!Visit(Id, 0))
            return false;
    for (const auto& Pin : (*Outputs)->Values)
        if (!Connections.Match(P4MaterialGraph::InputOf(Candidate, Pin.Key), Pin.Value, TEXT("material.") + Pin.Key))
            return false;
    // Explicit supported pin census, rather than traversing unrelated native
    // material structs as though they were expression inputs.
    for (const TCHAR* Pin : {TEXT("BaseColor"), TEXT("Metallic"), TEXT("Specular"), TEXT("Roughness"),
             TEXT("EmissiveColor"), TEXT("Opacity"), TEXT("OpacityMask"), TEXT("Normal"), TEXT("WorldPositionOffset"),
             TEXT("SubsurfaceColor"), TEXT("AmbientOcclusion"), TEXT("Refraction"), TEXT("PixelDepthOffset"),
             TEXT("ClearCoat"), TEXT("ClearCoatRoughness"), TEXT("Anisotropy"), TEXT("Tangent")})
        if (const auto* Actual = P4MaterialGraph::InputOf(Candidate, Pin))
            if (Actual->Expression && !(*Outputs)->HasField(Pin))
                return Fail(Error, FString(TEXT("extra-connected-pin:")) + Pin);
    for (UMaterialExpression* Expression : Candidate->Expressions)
        if (!Connections.Names.Contains(Expression) && !Connections.Helpers.Contains(Expression))
            return Fail(Error, TEXT("extra-expression"));
    TStrongObjectPtr<UMaterial> Expected(NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient));
    const TMap<FString, FString> Settings = {{TEXT("domain"), TEXT("MaterialDomain")},
        {TEXT("blend_mode"), TEXT("BlendMode")}, {TEXT("two_sided"), TEXT("TwoSided")},
        {TEXT("clip"), TEXT("OpacityMaskClipValue")}, {TEXT("tangent_space_normal"), TEXT("bTangentSpaceNormal")}};
    for (const auto& Entry : Settings)
    {
        FProperty* Field = FindFProperty<FProperty>(UMaterial::StaticClass(), *Entry.Value);
        if (!Field || !Source->HasField(Entry.Key) ||
            !P4MaterialGraph::Set(Expected.Get(), Field, Source->Values[Entry.Key], Error) ||
            !Field->Identical(
                Field->ContainerPtrToValuePtr<void>(Candidate), Field->ContainerPtrToValuePtr<void>(Expected.Get())))
            return Fail(Error, TEXT("setting-differs:") + Entry.Key);
    }
    FString Shading;
    if (!Source->TryGetStringField(TEXT("shading_model"), Shading) || !Candidate->GetShadingModels().IsValid() ||
        Candidate->GetShadingModels().CountShadingModels() != 1 ||
        StaticEnum<EMaterialShadingModel>()->GetNameStringByValue(
            int64(Candidate->GetShadingModels().GetFirstShadingModel())) != Shading)
        return Fail(Error, TEXT("shading-model-differs"));
    const TSharedPtr<FJsonObject>* Render = nullptr;
    if (Source->HasField(TEXT("render_settings")) && !Source->TryGetObjectField(TEXT("render_settings"), Render))
        return Fail(Error, TEXT("render-settings-unreadable"));
    if (Source->TryGetObjectField(TEXT("render_settings"), Render))
        for (const auto& Entry : (*Render)->Values)
        {
            UObject* Have = nullptr;
            UObject* Want = nullptr;
            FProperty* Field = P4MaterialGraph::RenderSettingProperty(Candidate, Entry.Key, false, Have);
            TMap<FString, TSharedPtr<FJsonValue>> GameValues;
            const TSharedPtr<FJsonObject>* SourceNodes = nullptr;
            const TSharedPtr<FJsonObject>* SourceOutputs = nullptr;
            if (!Field ||
                !P4MaterialGraph::SetRenderSetting(Expected.Get(), Entry.Key, Entry.Value,
                    Source->TryGetObjectField(TEXT("nodes"), SourceNodes)
                        ? P4MaterialGraph::HighestTexCoord(*SourceNodes,
                              Source->TryGetObjectField(TEXT("outputs"), SourceOutputs) ? *SourceOutputs : nullptr)
                        : -1,
                    GameValues, Error) ||
                !P4MaterialGraph::RenderSettingProperty(Expected.Get(), Entry.Key, false, Want) ||
                !Field->Identical(Field->ContainerPtrToValuePtr<void>(Have), Field->ContainerPtrToValuePtr<void>(Want)))
                return Fail(Error, TEXT("render-setting-differs:") + Entry.Key);
        }
    return true;
}
