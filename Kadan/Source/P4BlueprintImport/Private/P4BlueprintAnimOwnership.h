#pragma once
#include "P4BlueprintReimport.h"
#include "Animation/AnimBlueprint.h"
#include "AnimationGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationCustomTransitionGraph.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_BlendListByEnum.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "K2Node_Composite.h"
#include "K2Node_CallFunction.h"
#include "K2Node_AnimGetter.h"
#include "K2Node_MathExpression.h"
#include "K2Node_Tunnel.h"
#include "K2Node_Variable.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/SecureHash.h"
#include "Internationalization/Text.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Interface.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"

// Ownership is established only by the synchronous source construction below. Legacy object
// names and a general Blueprint import tag cannot identify animation objects a creator added.
namespace P4AnimOwnership
{
inline bool Marked(const UObject* Object, const TCHAR* Tag)
{ return Object && FString(Object->GetOutermost()->GetMetaData()->GetValue(Object, Tag)) == TEXT("1"); }
inline bool SourceNode(const UObject* Object) { return Marked(Object, TEXT("P4BlueprintSourceAnimationNode")); }
inline bool SourceGraph(const UObject* Object) { return Marked(Object, TEXT("P4BlueprintSourceAnimationGraph")); }
inline bool DerivedMathGraph(const UEdGraph* Graph)
{
    const auto Math = Graph ? Cast<UK2Node_MathExpression>(Graph->GetOuter()) : nullptr;
    return SourceGraph(Graph) && Math && Math->GetClass() == UK2Node_MathExpression::StaticClass() &&
        SourceNode(Math) && Math->BoundGraph == Graph && SourceNode(Math->GetEntryNode()) && SourceNode(Math->GetExitNode());
}
inline bool DerivedMathPin(const UEdGraphPin* Pin)
{
    auto Graph = Pin ? Pin->GetOwningNode()->GetGraph() : nullptr;
    return DerivedMathGraph(Graph) && Pin->GetOwningNode() != CastChecked<UK2Node_MathExpression>(Graph->GetOuter())->GetEntryNode();
}
inline bool SourceSubtreeNode(const UEdGraphNode* Node)
{ return SourceNode(Node) || (Node && DerivedMathGraph(Node->GetGraph())); }

inline bool NativeSelfPin(const UEdGraphPin* Pin)
{
    // Every function call node makes its self pin through UK2Node_CallFunction::CreateSelfPin, the array,
    // getter and other call kinds included.
    const auto Call = Pin ? Cast<UK2Node_CallFunction>(Pin->GetOwningNode()) : nullptr;
    if (!Call || !SourceSubtreeNode(Call) ||
        Pin != Call->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input)) return false;
    const UFunction* Function = Call->GetTargetFunction();
    if (!Function || !Function->HasAnyFunctionFlags(FUNC_Native)) return false;
    while (Function->GetSuperFunction()) Function = Function->GetSuperFunction();
    const auto Scope = Cast<UClass>(Function->GetOuter());
    if (!Scope || Scope->ClassGeneratedBy || !Scope->HasAnyClassFlags(CLASS_Native) || Scope->IsChildOf(UInterface::StaticClass())) return false;
    FEdGraphPinType Expected;
    Expected.PinCategory = UEdGraphSchema_K2::PC_Object; Expected.PinSubCategoryObject = Scope;
    // Engine's type equality deliberately omits bIsUObjectWrapper; all other scalar
    // self-type fields must match. The complete native export below retains every one.
    return Pin->PinType == Expected;
}

inline bool CanonicalPinText(FString& Text, const UEdGraphPin* Pin)
{
    // The exact native MathExpression producer deletes/recreates its generated function
    // nodes and output pins on every reconstruction. Keep all native fields except their
    // generated identities/references, whose complete ordered topology is recorded below.
    if (Text.Len() < 2 || Text[0] != TCHAR('(') || Text[Text.Len() - 1] != TCHAR(')')) return false;
    const bool Math = DerivedMathGraph(Pin->GetOwningNode()->GetGraph());
    const bool NativeSelf = NativeSelfPin(Pin);
    if (!Math && !NativeSelf) return true;
    FString Out = TEXT("(");
    int32 Depth = 0, Start = 1; bool Quoted = false, SawId = false, SawWrapper = false;
    const auto Field = [&](int32 End)
    {
        const FString Value = Text.Mid(Start, End - Start);
        if (Value.IsEmpty()) return true;
        const int32 Equal = Value.Find(TEXT("="), ESearchCase::CaseSensitive);
        if (Equal < 1) return false;
        const FString Key = Value.Left(Equal);
        if (NativeSelf && Key == TEXT("PinType.bIsUObjectWrapper"))
        {
            // UE4.26's parameterized FEdGraphPinType ctor used by CreateSelfPin omits
            // initialization of this bit. A self pin has no parameter FProperty producer.
            if (SawWrapper || (Value.Mid(Equal + 1) != TEXT("True") && Value.Mid(Equal + 1) != TEXT("False"))) return false;
            SawWrapper = true; Out += Key + TEXT("=False,"); return true;
        }
        if (Key == TEXT("PinId"))
        {
            if (SawId || Value.Mid(Equal + 1) != Pin->PinId.ToString(EGuidFormats::Digits)) return false;
            SawId = true;
            if (!Math || !DerivedMathPin(Pin)) Out += Value + TEXT(","); // authored input identity is retained
        }
        else if (!Math || (Key != TEXT("LinkedTo") && Key != TEXT("SubPins") && Key != TEXT("ParentPin") && Key != TEXT("ReferencePassThroughConnection")))
            Out += Value + TEXT(",");
        return true;
    };
    for (int32 Index = 0; Index < Text.Len(); ++Index)
    {
        const TCHAR C = Text[Index];
        if (Quoted && C == TCHAR('\\')) { if (++Index >= Text.Len()) return false; continue; }
        if (C == TCHAR('"')) { Quoted = !Quoted; continue; }
        if (Quoted) continue;
        if (C == TCHAR('(')) ++Depth;
        else if (C == TCHAR(')'))
        { if (--Depth < 0) return false; if (!Depth && (Index != Text.Len() - 1 || !Field(Index))) return false; }
        else if (C == TCHAR(',') && Depth == 1) { if (!Field(Index)) return false; Start = Index + 1; }
    }
    if (Quoted || Depth || !SawId || (NativeSelf && !SawWrapper)) return false;
    Text = Out + TEXT(")");
    return true;
}
inline UAnimationGraph* Top(UBlueprint* Blueprint)
{
    UAnimationGraph* Found = nullptr;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs) if (auto Animation = Cast<UAnimationGraph>(Graph))
    { if (Found) return nullptr; Found = Animation; }
    return Found;
}

inline bool CanonicalEditorLabel(const FText& Text, FText& Canonical)
{
    // Saving these editor display labels creates empty-namespace random localization keys;
    // loading reconstructs them from metadata. Keep source text; retain authored identities.
    if (Text.IsFromStringTable() || Text.IsCultureInvariant()) return false;
    const auto Namespace = FTextInspector::GetNamespace(Text);
    const auto Key = FTextInspector::GetKey(Text);
    FGuid AutoKey;
    if ((Namespace.IsSet() && !Namespace.GetValue().IsEmpty()) || (Key.IsSet() && !Key.GetValue().IsEmpty() && !FGuid::ParseExact(Key.GetValue(), EGuidFormats::Digits, AutoKey))) return false;
    const FString* Source = FTextInspector::GetSourceString(Text);
    if (!Source) return false;
    // Only the two base-string representations generated for these exact editor labels.
    // Format/number/date/transform histories carry authored semantics even at equal display text.
    FString Actual, Expected;
    FTextStringHelper::WriteToBuffer(Actual, Text, true);
    FText Base = FText::FromString(*Source);
    if (Key.IsSet() && !Key.GetValue().IsEmpty()) Base = FText::ChangeKey(TEXT(""), Key.GetValue(), Base);
    FTextStringHelper::WriteToBuffer(Expected, Base, true);
    if (Actual != Expected) return false;
    Canonical = FText::AsCultureInvariant(*Source);
    return true;
}

inline int32 CanonicalPinLabel(const UEdGraphPin* Pin, FText& Canonical)
{
    if (CanonicalEditorLabel(Pin->PinFriendlyName, Canonical)) return 1;
    FString Original;
    FTextStringHelper::WriteToBuffer(Original, Pin->PinFriendlyName, true);
    if (!Original.StartsWith(TEXT("LOCGEN_FORMAT_NAMED("))) return 0;
    TArray<FHistoricTextFormatData> Formats;
    FTextInspector::GetHistoricFormatData(Pin->PinFriendlyName, Formats);
    for (const auto& Format : Formats)
    {
        if (!Format.FormattedText.IdenticalTo(Pin->PinFriendlyName, ETextIdenticalModeFlags::DeepCompare)) continue;
        const FText Pattern = Format.SourceFmt.GetSourceText();
        const auto Namespace = FTextInspector::GetNamespace(Pattern), Key = FTextInspector::GetKey(Pattern);
        const FString* Source = FTextInspector::GetSourceString(Pattern);
        if (!Namespace.IsSet() || !Key.IsSet() || !Source) return 0;
        FString Argument;
        if (Namespace.GetValue() == TEXT("K2Node") && Key.GetValue() == TEXT("PinFriendlyNameWithIndex") && *Source == TEXT("{PinName} {Index}") && Format.Arguments.Num() == 2)
        {
            auto Index = Format.Arguments.Find(TEXT("Index"));
            if (!Index || Index->GetType() != EFormatArgumentType::Int ||
                !Pin->PinName.ToString().EndsWith(TEXT("_") + FString::Printf(TEXT("%lld"), (long long)Index->GetIntValue()))) return 0;
            Argument = TEXT("PinName");
        }
        else if (Pin->GetOwningNode()->GetClass() == UAnimGraphNode_BlendListByEnum::StaticClass() && Namespace.GetValue() == TEXT("BlendListByEnum") && Format.Arguments.Num() == 1 &&
            ((Key.GetValue() == TEXT("FriendlyNamePose") && *Source == TEXT("{PinFriendlyName} Pose") && Pin->PinName.ToString().StartsWith(TEXT("BlendPose_"))) ||
             (Key.GetValue() == TEXT("FriendlyNameBlendTime") && *Source == TEXT("{PinFriendlyName} Blend Time") && Pin->PinName.ToString().StartsWith(TEXT("BlendTime_")))))
            Argument = TEXT("PinFriendlyName");
        else return 0;
        auto Value = Format.Arguments.Find(Argument);
        if (!Value || Value->GetType() != EFormatArgumentType::Text) return 0;
        FText Leaf;
        if (!CanonicalEditorLabel(Value->GetTextValue(), Leaf)) return 0;
        FString Rebuilt;
        FTextStringHelper::WriteToBuffer(Rebuilt, FText::Format(Format.SourceFmt, Format.Arguments), true);
        if (Rebuilt != Original) return 0; // exact format history, order, argument names/types
        FFormatNamedArguments Arguments = Format.Arguments;
        Arguments.FindChecked(Argument) = FFormatArgumentValue(Leaf);
        Canonical = FText::Format(Format.SourceFmt, Arguments);
        return 2;
    }
    return 0;
}

inline FString PropertyText(FProperty* Property, const void* Value, UObject* Owner, bool NormalizeEditorLabels)
{
    FString Text;
    const auto Array = CastField<FArrayProperty>(Property);
    const auto Struct = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
    const auto Tooltip = Struct && Struct->Struct->GetPathName() == TEXT("/Script/BlueprintGraph.OptionalPinFromProperty")
        ? FindFProperty<FTextProperty>(Struct->Struct, TEXT("PropertyTooltip")) : nullptr;
    if (!NormalizeEditorLabels || !Tooltip || Property->ArrayDim != 1)
    { Property->ExportTextItem(Text, Value, nullptr, Owner, PPF_Delimited); return Text; }
    // Normalize a value copy, never the source object or its shared text data.
    void* Copy = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
    Property->InitializeValue(Copy); Property->CopyCompleteValue(Copy, Value);
    FScriptArrayHelper Items(Array, Copy);
    FString LabelKinds;
    for (int32 Index = 0; Index < Items.Num(); ++Index)
    {
        FText Canonical;
        const bool GeneratedLabel = CanonicalEditorLabel(Tooltip->GetPropertyValue_InContainer(Items.GetRawPtr(Index)), Canonical);
        LabelKinds += GeneratedLabel ? TEXT("B") : TEXT("S");
        if (GeneratedLabel)
            Tooltip->SetPropertyValue_InContainer(Items.GetRawPtr(Index), Canonical);
    }
    Property->ExportTextItem(Text, Copy, nullptr, Owner, PPF_Delimited);
    Property->DestroyValue(Copy); FMemory::Free(Copy);
    // This separate schema suffix distinguishes normalized base strings from authored INVTEXT.
    return LabelKinds + TEXT(":") + Text;
}

inline bool Fingerprint(UBlueprint* Blueprint, FString& Digest, TArray<FString>* Witness = nullptr, bool NormalizeEditorLabels = true)
{
    FString Content;
    const auto Add = [&](const FString& Value)
    { Content += FString::FromInt(Value.Len()) + TEXT(":") + Value; if (Witness) Witness->Add(Value); };
    // UE numeric ExportText uses six decimal places. Preserve every saved IEEE bit as well,
    // including one-ULP creator edits, signed zero and noncanonical NaN payloads.
    bool NumbersComplete = true;
    TFunction<void(FProperty*, const void*, int32)> ExactNumbers;
    ExactNumbers = [&](FProperty* Property, const void* Value, int32 Depth)
    {
        if (Depth > 64) { NumbersComplete = false; return; }
        if (CastField<FFloatProperty>(Property) || CastField<FDoubleProperty>(Property))
        { Add(Property->GetPathName()); Add(BytesToHex(static_cast<const uint8*>(Value), Property->ElementSize)); }
        else if (auto Struct = CastField<FStructProperty>(Property))
        {
            for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
                if (!It->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization))
                    for (int32 Index = 0; Index < It->ArrayDim; ++Index)
                        ExactNumbers(*It, It->ContainerPtrToValuePtr<void>(Value, Index), Depth + 1);
        }
        else if (auto Array = CastField<FArrayProperty>(Property))
        { FScriptArrayHelper Items(Array, Value); for (int32 I = 0; I < Items.Num(); ++I) ExactNumbers(Array->Inner, Items.GetRawPtr(I), Depth + 1); }
        else if (auto Map = CastField<FMapProperty>(Property))
        { FScriptMapHelper Items(Map, Value); for (int32 I = 0; I < Items.GetMaxIndex(); ++I) if (Items.IsValidIndex(I))
            { ExactNumbers(Map->KeyProp, Items.GetKeyPtr(I), Depth + 1); ExactNumbers(Map->ValueProp, Items.GetValuePtr(I), Depth + 1); } }
        else if (auto Set = CastField<FSetProperty>(Property))
        { FScriptSetHelper Items(Set, Value); for (int32 I = 0; I < Items.GetMaxIndex(); ++I) if (Items.IsValidIndex(I)) ExactNumbers(Set->ElementProp, Items.GetElementPtr(I), Depth + 1); }
    };
    bool ReferencesComplete = true;
    const auto NodeIdentity = [&](const UEdGraphNode* Node)
    {
        if (!DerivedMathGraph(Node->GetGraph()) || SourceNode(Node)) return Node->GetPathName(Blueprint);
        const int32 Index = Node->GetGraph()->Nodes.IndexOfByKey(Node);
        if (Index == INDEX_NONE) ReferencesComplete = false;
        return Node->GetGraph()->GetPathName(Blueprint) + TEXT("#node=") + FString::FromInt(Index);
    };
    const auto Properties = [&](UObject* Object, bool Graph)
    {
        const auto Node = Cast<UEdGraphNode>(Object);
        Add(Node ? NodeIdentity(Node) : Object->GetPathName(Blueprint)); Add(Object->GetClass()->GetPathName());
        for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
        {
            if (It->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization)) continue;
            if (Graph && (It->GetFName() == TEXT("Nodes") || It->GetFName() == TEXT("SubGraphs"))) continue;
            if (Node && DerivedMathGraph(Node->GetGraph()) && !SourceNode(Node) &&
                *It == FindFProperty<FProperty>(UEdGraphNode::StaticClass(), TEXT("NodeGuid"))) continue;
            Add(It->GetPathName());
            Add(It->GetCPPType()); Add(FString::FromInt(It->ArrayDim)); Add(FString::FromInt(It->ElementSize));
            for (int32 Index = 0; Index < It->ArrayDim; ++Index)
            {
                Add(PropertyText(*It, It->ContainerPtrToValuePtr<void>(Object, Index), Object, NormalizeEditorLabels));
                ExactNumbers(*It, It->ContainerPtrToValuePtr<void>(Object, Index), 0);
            }
        }
    };
    const auto PinReference = [&](const UEdGraphPin* Pin)
    {
        Add(Pin ? NodeIdentity(Pin->GetOwningNode()) : FString());
        if (DerivedMathPin(Pin))
        {
            const int32 Index = Pin->GetOwningNode()->Pins.IndexOfByKey(Pin);
            if (Index == INDEX_NONE) ReferencesComplete = false;
            Add(TEXT("derived-pin=") + FString::FromInt(Index));
        }
        else Add(Pin ? Pin->PinId.ToString() : FString());
    };
    // Declaration/animation construction also replace these animation-owned saved fields.
    // Their creator edits need the same early guard as the nodes that refer to them.
    if (!Blueprint->IsA<UAnimBlueprint>()) return false;
    for (const TCHAR* Name : {TEXT("TargetSkeleton"), TEXT("Groups")})
    {
        auto Field = FindFProperty<FProperty>(UAnimBlueprint::StaticClass(), Name);
        if (!Field) return false;
        Add(Field->GetPathName()); Add(Field->GetCPPType()); Add(FString::FromInt(Field->ArrayDim)); Add(FString::FromInt(Field->ElementSize));
        for (int32 Index = 0; Index < Field->ArrayDim; ++Index)
        {
            Add(PropertyText(Field, Field->ContainerPtrToValuePtr<void>(Blueprint, Index), Blueprint, NormalizeEditorLabels));
            ExactNumbers(Field, Field->ContainerPtrToValuePtr<void>(Blueprint, Index), 0);
        }
    }
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    Graphs.Sort([&](const UEdGraph& A, const UEdGraph& B) { return A.GetPathName(Blueprint) < B.GetPathName(Blueprint); });
    int32 Count = 0;
    for (UEdGraph* Graph : Graphs) if (Graph && SourceGraph(Graph))
    {
        ++Count; Properties(Graph, true);
        // Independent creator additions do not change the source-only ordered membership.
        Add(TEXT("source-subgraphs"));
        for (UEdGraph* Child : Graph->SubGraphs) if (Child && SourceGraph(Child)) Add(Child->GetPathName(Blueprint));
        Add(TEXT("source-nodes"));
        for (UEdGraphNode* Node : Graph->Nodes) if (Node && SourceSubtreeNode(Node))
        {
            Properties(Node, false);
            // EditablePinBase has a native saved tail in addition to its reflected fields.
            if (auto Editable = Cast<UK2Node_EditablePinBase>(Node))
            {
                Add(TEXT("native-user-defined-pins")); Add(FString::FromInt(Editable->UserDefinedPins.Num()));
                for (const auto& Info : Editable->UserDefinedPins)
                {
                    if (!Info.IsValid()) return false;
                    Add(Info->PinName.ToString()); Add(FString::FromInt(Info->DesiredPinDirection)); Add(Info->PinDefaultValue);
                    FString Type;
                    FEdGraphPinType::StaticStruct()->ExportText(Type, &Info->PinType, nullptr, Node, PPF_Delimited, nullptr);
                    Add(Type);
                }
            }
            for (const UEdGraphPin* Pin : Node->Pins) if (Pin)
            {
                FString Text;
                if (!Pin->ExportTextItem(Text, PPF_Delimited)) return false;
                FText Friendly;
                const int32 GeneratedLabel = NormalizeEditorLabels && !Pin->PinFriendlyName.IsEmpty() ? CanonicalPinLabel(Pin, Friendly) : 0;
                Add(GeneratedLabel == 1 ? TEXT("base-editor-pin-label") : GeneratedLabel == 2 ? TEXT("native-format-editor-pin-label") : TEXT("strict-editor-pin-label"));
                if (GeneratedLabel != 0)
                {
                    FString Original, Normalized;
                    FTextStringHelper::WriteToBuffer(Original, Pin->PinFriendlyName, true);
                    FTextStringHelper::WriteToBuffer(Normalized, Friendly, true);
                    Original = TEXT("PinFriendlyName=") + Original;
                    const int32 At = Text.Find(Original, ESearchCase::CaseSensitive);
                    if (At < 1 || (Text[At - 1] != TCHAR(',') && Text[At - 1] != TCHAR('(')) ||
                        At + Original.Len() >= Text.Len() || Text[At + Original.Len()] != TCHAR(',') ||
                        Text.Find(Original, ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 1) != INDEX_NONE) return false;
                    Text = Text.Left(At) + TEXT("PinFriendlyName=") + Normalized + Text.Mid(At + Original.Len());
                }
                if (!CanonicalPinText(Text, Pin)) return false;
                Add(Text);
                // Native pin text uses node names; full relative paths disambiguate graphs.
                PinReference(Pin);
                Add(TEXT("linked-pins")); Add(FString::FromInt(Pin->LinkedTo.Num()));
                for (const UEdGraphPin* Linked : Pin->LinkedTo) PinReference(Linked);
                Add(TEXT("sub-pins")); Add(FString::FromInt(Pin->SubPins.Num()));
                for (const UEdGraphPin* Sub : Pin->SubPins) PinReference(Sub);
                Add(TEXT("parent-pin"));
                PinReference(Pin->ParentPin); Add(TEXT("reference-pass-through-pin"));
                PinReference(Pin->ReferencePassThroughConnection);
            }
        }
    }
    if (!Count || !NumbersComplete || !ReferencesComplete) return false;
    const FTCHARToUTF8 Bytes(Content.GetCharArray().GetData(), Content.Len());
    uint8 Hash[FSHA1::DigestSize];
    FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash);
    Digest = BytesToHex(Hash, FSHA1::DigestSize);
    return true;
}

// An animation Blueprint a 0.2.0 project imported has no ownership record. Kadan 0.2.0 kept every
// imported game Blueprint locked (its original record, a read-only file, and the guards that put any
// change back), so its animation graph holds only what the importer made from the game: all of it is
// source. A copy carries a record that names another package and is not this.
inline bool LegacyGameOriginal(UBlueprint* Blueprint)
{
    if (!Blueprint || Marked(Blueprint, TEXT("P4BlueprintAnimationOwnershipVersion")) || Blueprint->GetOutermost()->IsDirty()) return false;
    TSharedPtr<FJsonObject> Record;
    FString Package;
    const FString Text = Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint, TEXT("P4OriginalRecord"));
    if (Text.IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Record) || !Record.IsValid() ||
        !Record->TryGetStringField(TEXT("package"), Package) || !Package.Equals(Blueprint->GetOutermost()->GetName(), ESearchCase::CaseSensitive))
        return false;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs) if (Graph)
        for (UEdGraphNode* Node : Graph->Nodes) if (Marked(Node, TEXT("P4BlueprintCreatorNode"))) return false;
    return true;
}

inline bool CaptureBaseline(UBlueprint* Blueprint, FString& Why)
{
    FString Digest;
    if (!Marked(Blueprint, TEXT("P4BlueprintAnimationOwnershipVersion")) || !Fingerprint(Blueprint, Digest))
    { Why = TEXT("animation-source-content-baseline-unavailable"); return false; }
    Blueprint->GetOutermost()->GetMetaData()->SetValue(Blueprint, TEXT("P4BlueprintAnimationSourceContent"), *Digest);
    return true;
}

inline bool Preflight(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Plan, FString& Why, bool Initializing = false)
{
    if (!Blueprint || !Plan.IsValid() || !Plan->HasTypedField<EJson::Object>(TEXT("anim"))) return true;
    if (Initializing) return true;
    if (LegacyGameOriginal(Blueprint)) return true;
    if (!Marked(Blueprint, TEXT("P4BlueprintAnimationOwnershipVersion")))
    {
        Why = TEXT("legacy-animation-ownership-unrecorded: existing animation graphs have no source ownership record; keep this asset unchanged and import the original into a fresh project");
        return false;
    }
    UAnimationGraph* Root = Top(Blueprint);
    TArray<UAnimGraphNode_Root*> Results;
    if (Root) Root->GetNodesOfClass(Results);
    if (!Root || !SourceGraph(Root) || Results.Num() != 1 || !SourceNode(Results[0]))
    { Why = TEXT("animation-source-root-ownership-missing"); return false; }
    FString Current;
    const FString Baseline = Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint, TEXT("P4BlueprintAnimationSourceContent"));
    if (Baseline.Len() != 40 || !Fingerprint(Blueprint, Current))
    { Why = TEXT("animation-source-content-baseline-unavailable"); return false; }
    if (!Baseline.Equals(Current, ESearchCase::CaseSensitive))
    { Why = TEXT("animation-source-content-changed: preserve the saved animation-node properties, pins and graph edits"); return false; }
    TSharedPtr<FJsonObject> Previous;
    const FString PreviousText = Blueprint->GetOutermost()->GetMetaData()->GetValue(Blueprint, TEXT("P4BlueprintSourceMembers"));
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PreviousText), Previous) || !Previous.IsValid())
    { Why = TEXT("animation-source-members-unavailable"); return false; }
    TSet<FName> EventGraphs;
    const TArray<TSharedPtr<FJsonValue>>* PreviousGraphs = nullptr;
    if (!Previous->TryGetArrayField(TEXT("graphs"), PreviousGraphs))
    { Why = TEXT("animation-source-members-unavailable"); return false; }
    for (const auto& Item : *PreviousGraphs) EventGraphs.Add(FName(*Item->AsString()));
    for (const auto& Item : Plan->GetArrayField(TEXT("graphs")))
        if (Item->AsObject()->GetStringField(TEXT("kind")) == TEXT("event")) EventGraphs.Add(FName(*Item->AsObject()->GetStringField(TEXT("name"))));
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    const auto AdmittedChild = [&](UEdGraph* Child)
    {
        if (!Child || SourceGraph(Child)) return true;
        Why = TEXT("animation-creator-graph-in-source-subtree: ") + Child->GetPathName(Blueprint);
        return false;
    };
    TSet<UObject*> Retiring;
    // These selected source event nodes are also replaced by ReuseBlueprint. Their
    // retained event container does not make creator links/references into them safe.
    for (UEdGraph* Graph : Blueprint->UbergraphPages) if (Graph && EventGraphs.Contains(Graph->GetFName()))
        for (UEdGraphNode* Node : Graph->Nodes)
            if (Marked(Node, TEXT("P4BlueprintSourceNode")) && !Marked(Node, TEXT("P4BlueprintCreatorNode"))) Retiring.Add(Node);
    for (UEdGraph* Graph : Graphs) if (Graph && SourceGraph(Graph))
    {
        // Engine removal follows graph references as well as UObject outers. A creator child
        // with an independent Outer must not evade this boundary and be removed recursively.
        for (UEdGraph* Child : Graph->SubGraphs) if (!AdmittedChild(Child)) return false;
        if (Graph != Root) Retiring.Add(Graph);
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && SourceSubtreeNode(Node))
            {
                if (auto Machine = Cast<UAnimGraphNode_StateMachineBase>(Node))
                    if (!AdmittedChild(Machine->EditorStateMachineGraph)) return false;
                if (auto State = Cast<UAnimStateNodeBase>(Node))
                    if (!AdmittedChild(State->GetBoundGraph())) return false;
                if (auto Transition = Cast<UAnimStateTransitionNode>(Node))
                    if (!AdmittedChild(Transition->CustomTransitionGraph)) return false;
                if (auto Composite = Cast<UK2Node_Composite>(Node))
                    if (!AdmittedChild(Composite->BoundGraph)) return false;
            }
            if (Node && SourceSubtreeNode(Node) && Node != Results[0]) Retiring.Add(Node);
            else if (Node && Graph != Root)
            { Why = TEXT("animation-creator-node-in-source-subtree: ") + Node->GetPathName(Blueprint); return false; }
        }
    }
    TSet<FName> PlannedNames;
    TSet<FName> SourceVariables;
    for (const auto& Item : Plan->GetArrayField(TEXT("variables"))) SourceVariables.Add(FName(*Item->AsObject()->GetStringField(TEXT("name"))));
    const TArray<TSharedPtr<FJsonValue>>* PreviousVariables = nullptr;
    if (!Previous->TryGetArrayField(TEXT("variables"), PreviousVariables))
    { Why = TEXT("animation-source-members-unavailable"); return false; }
    for (const auto& Item : *PreviousVariables) SourceVariables.Add(FName(*Item->AsString()));
    const auto Animation = Plan->GetObjectField(TEXT("anim"));
    for (const auto& Item : Animation->GetObjectField(TEXT("graph"))->GetArrayField(TEXT("nodes")))
        PlannedNames.Add(FName(*Item->AsObject()->GetStringField(TEXT("name"))));
    for (UEdGraph* Graph : Graphs) if (Graph)
    {
        if (!SourceGraph(Graph))
            for (UObject* Outer = Graph->GetOuter(); Outer && Outer != Blueprint; Outer = Outer->GetOuter())
                if (Retiring.Contains(Outer) || Outer == Root)
                { Why = TEXT("animation-creator-graph-in-source-subtree: ") + Graph->GetPathName(Blueprint); return false; }
        for (UEdGraphNode* Node : Graph->Nodes) if (Node && !SourceSubtreeNode(Node) && !Retiring.Contains(Node))
        {
            if (auto Variable = Cast<UK2Node_Variable>(Node))
                if (!Marked(Node, TEXT("P4BlueprintSourceNode")) && Variable->VariableReference.IsSelfContext() && SourceVariables.Contains(Variable->GetVarName()))
                { Why = TEXT("animation-creator-node-depends-on-source-variable: ") + Node->GetPathName(Blueprint); return false; }
            if (Graph == Root && PlannedNames.Contains(Node->GetFName()))
            { Why = TEXT("animation-creator-node-source-name-collision: ") + Node->GetPathName(Blueprint); return false; }
            // This includes authored UObject references such as a cached-pose/getter source,
            // as well as explicit pin links. Do not silently break creator dependencies.
            TArray<UObject*> References;
            FReferenceFinder Finder(References, Blueprint, false, true, false, false);
            Finder.FindReferences(Node);
            for (UObject* Reference : References) if (Retiring.Contains(Reference))
            { Why = TEXT("animation-creator-node-depends-on-source: ") + Node->GetPathName(Blueprint); return false; }
            for (UEdGraphPin* Pin : Node->Pins) if (Pin)
                for (UEdGraphPin* Linked : Pin->LinkedTo)
                    if (Linked && (Retiring.Contains(Linked->GetOwningNode()) || Linked->GetOwningNode() == Results[0]))
                    { Why = TEXT("animation-creator-node-depends-on-source: ") + Node->GetPathName(Blueprint); return false; }
        }
    }
    return true;
}

inline bool Reset(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Plan, FString& Why)
{
    const bool Initializing = P4Reimport::CanInitializeAnimationOwnership(Blueprint) && !Marked(Blueprint, TEXT("P4BlueprintAnimationOwnershipVersion"));
    if (!Preflight(Blueprint, Plan, Why, Initializing)) return false;
    if (Initializing) return true;
    auto Root = Top(Blueprint);
    TArray<UAnimGraphNode_Root*> Results;
    if (Root) Root->GetNodesOfClass(Results);
    if (!Root || Results.Num() != 1) { Why = TEXT("animation-source-root-ownership-missing"); return false; }
    // A 0.2.0 game Blueprint: everything but the output pose is the importer's, so all of it is built again
    // and recorded as source, as a fresh import records it.
    if (LegacyGameOriginal(Blueprint))
    {
        for (UEdGraphNode* Node : TArray<UEdGraphNode*>(Root->Nodes))
            if (Node && Node != Results[0]) FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
        return true;
    }
    // Removing an owned state-machine node uses the engine's DestroyNode/RemoveGraph lifecycle
    // for its complete admitted subtree. The root and independent creator nodes remain alive.
    for (UEdGraphNode* Node : TArray<UEdGraphNode*>(Root->Nodes))
        if (Node && SourceNode(Node) && Node != Results[0]) FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
        else if (Node && !SourceNode(Node))
            Node->GetOutermost()->GetMetaData()->SetValue(Node, TEXT("P4BlueprintCreatorNode"), TEXT("1"));
    return true;
}

struct FCreated
{
    TSet<UEdGraph*> Graphs;
    TSet<UEdGraphNode*> Nodes;
    explicit FCreated(UBlueprint* Blueprint)
    {
        TArray<UEdGraph*> Existing;
        Blueprint->GetAllGraphs(Existing);
        for (UEdGraph* Graph : Existing) if (Graph)
        { Graphs.Add(Graph); for (UEdGraphNode* Node : Graph->Nodes) if (Node) Nodes.Add(Node); }
    }
    void Mark(UBlueprint* Blueprint, UAnimationGraph* Root, UAnimGraphNode_Root* Result) const
    {
        auto Metadata = Blueprint->GetOutermost()->GetMetaData();
        TArray<UEdGraph*> Current;
        Blueprint->GetAllGraphs(Current);
        Metadata->SetValue(Root, TEXT("P4BlueprintSourceAnimationGraph"), TEXT("1"));
        Metadata->SetValue(Root, TEXT("P4BlueprintSourceGraph"), TEXT("1"));
        Metadata->SetValue(Result, TEXT("P4BlueprintSourceAnimationNode"), TEXT("1"));
        for (UEdGraph* Graph : Current) if (Graph)
        {
            if (!Graphs.Contains(Graph))
            {
                Metadata->SetValue(Graph, TEXT("P4BlueprintSourceAnimationGraph"), TEXT("1"));
                Metadata->SetValue(Graph, TEXT("P4BlueprintSourceGraph"), TEXT("1"));
            }
            for (UEdGraphNode* Node : Graph->Nodes) if (Node && !Nodes.Contains(Node))
                Metadata->SetValue(Node, TEXT("P4BlueprintSourceAnimationNode"), TEXT("1"));
        }
        // Retained independent nodes preceded the newly rebuilt source nodes. Keep the
        // source's original construction order contiguous, followed by creator nodes in
        // their unchanged relative order. UE's compiler prunes unlinked pure nodes with
        // RemoveAtSwap; interleaving them would permute source animation allocation IDs.
        TArray<UEdGraphNode*> Ordered;
        for (UEdGraphNode* Node : Root->Nodes) if (SourceNode(Node)) Ordered.Add(Node);
        for (UEdGraphNode* Node : Root->Nodes) if (!SourceNode(Node)) Ordered.Add(Node);
        Root->Nodes = MoveTemp(Ordered);
        Metadata->SetValue(Blueprint, TEXT("P4BlueprintAnimationOwnershipVersion"), TEXT("1"));
    }
};
}
