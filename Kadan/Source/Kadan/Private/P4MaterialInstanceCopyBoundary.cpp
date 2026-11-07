// The boundary of a copied material instance: the values a cook writes for it, read back and
// checked against the game's file.

#include "P4LevelContentInternal.h"

namespace P4LevelContent
{
// A different contract from ObjectState: this closes raw serialized storage for
// the conservative copied-MIC content comparison and performs no projection.
bool CopyMICDecodedFields(const UStruct* Type, const FDecodedValue& Value, int32 Depth)
{
    if (!Type || Depth > 32 || Value.Kind != FDecodedValue::EKind::Object)
        return false;
    const auto MemberValue = [&](const auto& Self, const FProperty* P, const FDecodedValue& V, int32 At) -> bool
    {
        if (!P || At > 32)
            return false;
        if (const auto* S = CastField<FStructProperty>(P))
        {
            if (S->Struct->GetPathName() == TEXT("/Script/CoreUObject.SoftObjectPath"))
                return V.Kind == FDecodedValue::EKind::SoftPath || V.Kind == FDecodedValue::EKind::String;
            return CopyMICDecodedFields(S->Struct, V, At + 1);
        }
        if (const auto* A = CastField<FArrayProperty>(P))
        {
            if (V.Kind != FDecodedValue::EKind::Array || V.Items.size() > 100000)
                return false;
            for (const auto& Child : V.Items)
                if (!Self(Self, A->Inner, Child, At + 1))
                    return false;
            return true;
        }
        if (const auto* M = CastField<FMapProperty>(P))
        {
            const auto* Pairs = V.Field("__native_map_pairs");
            if (V.Kind != FDecodedValue::EKind::Object || V.Fields.size() != 1 || !Pairs ||
                Pairs->Kind != FDecodedValue::EKind::Array || Pairs->Items.size() > 100000)
                return false;
            for (const auto& Pair : Pairs->Items)
                if (Pair.Kind != FDecodedValue::EKind::Array || Pair.Items.size() != 2 ||
                    !Self(Self, M->KeyProp, Pair.Items[0], At + 1) || !Self(Self, M->ValueProp, Pair.Items[1], At + 1))
                    return false;
            return true;
        }
        if (!V.Fields.empty() || !V.Items.empty())
            return false;
        using K = FDecodedValue::EKind;
        if (CastField<FBoolProperty>(P))
            return V.Kind == K::Boolean;
        if (CastField<FByteProperty>(P) || CastField<FEnumProperty>(P))
            return V.Kind == K::Number || V.Kind == K::String;
        if (CastField<FNumericProperty>(P))
            return V.Kind == K::Number;
        if (CastField<FNameProperty>(P) || CastField<FStrProperty>(P) || CastField<FSoftObjectProperty>(P))
            return V.Kind == K::String;
        if (CastField<FObjectProperty>(P))
            return V.Kind == K::String || V.Kind == K::Null;
        // Text histories and any unfamiliar scalar decoder representation are
        // outside this initial cooked subset; never accept an opaque atom.
        return false;
    };
    TSet<FString> Seen;
    for (const auto& Field : Value.Fields)
    {
        FString Name = Text(Field.first);
        int32 Index = 0, Bracket = INDEX_NONE;
        if (Name.FindChar(TEXT('['), Bracket))
        {
            if (!Name.EndsWith(TEXT("]")))
                return false;
            const FString Number = Name.Mid(Bracket + 1, Name.Len() - Bracket - 2);
            Index = FCString::Atoi(*Number);
            if (Index < 0 || FString::FromInt(Index) != Number)
                return false;
            Name.LeftInline(Bracket);
        }
        const FProperty* P = Type->FindPropertyByName(FName(*Name));
        const FString Identity = Name + TEXT("[") + FString::FromInt(Index) + TEXT("]");
        if (!P || !P4ExactNativeString(P->GetName(), Name) || Index >= P->ArrayDim || Seen.Contains(Identity) ||
            P->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization) ||
            !MemberValue(MemberValue, P, Field.second, Depth + 1))
            return false;
        Seen.Add(Identity);
    }
    return true;
}

bool CopyMICCookFrame(UObject* Asset, const UObject* Role, const FFile& File, FString& Error)
{
    Error.Reset();
    const auto Refuse = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    FString Origin;
    if (!IsInGameThread() || !IsValid(Asset) || !IsValid(Role) ||
        !P4ExactNativeString(Asset->GetClass()->GetPathName(), TEXT("/Script/Engine.MaterialInstanceConstant")) ||
        Role->GetClass() != UP4CreatorSourceData::StaticClass() || Role->GetOuter() != Asset ||
        !P4HasIntrinsicGameSource(Asset, &Origin) || P4HasIntrinsicGameProxy(Asset))
        return Refuse(TEXT("copy-MIC-cook-origin-not-qualified"));
    if (File.Nodes.Num() < 1 || File.Nodes.Num() > 2)
        return Refuse(TEXT("copy-MIC-extra-cooked-exports"));
    int32 Root = 0, Roles = 0;
    for (int32 At = 0; At < File.Nodes.Num(); ++At)
        if (P4ExactNativeString(File.Nodes[At].Class, TEXT("/Script/Engine.MaterialInstanceConstant")) &&
            P4ExactNativeString(File.Nodes[At].Name, Asset->GetName()) && !File.Nodes[At].Outer)
        {
            if (Root)
                return Refuse(TEXT("copy-MIC-duplicate-root"));
            Root = At + 1;
        }
    if (!Root)
        return Refuse(TEXT("copy-MIC-cooked-root-not-qualified"));
    for (int32 At = 0; At < File.Nodes.Num(); ++At)
    {
        const FNode& Node = File.Nodes[At];
        const UStruct* Type = Asset->GetClass();
        if (At + 1 != Root)
        {
            if (++Roles > 1 || Node.Outer != Root || !P4ExactNativeString(Node.Name, Role->GetName()) ||
                !P4ExactNativeString(Node.Class, Role->GetClass()->GetPathName()) || Node.Flags != 0)
                return Refuse(TEXT("copy-MIC-extra-cooked-object"));
            Type = Role->GetClass();
        }
        else if (Node.Flags != uint32(RF_Public | RF_Standalone))
            return Refuse(TEXT("copy-MIC-cooked-root-flags"));
        const UObject* Live = At + 1 == Root ? Asset : Role;
        const UObject* CDO = Live->GetClass()->GetDefaultObject(false);
        std::string Template;
        if (!CDO || Live->GetArchetype() != CDO ||
            (Node.Template &&
                (!File.Package.Reference(Node.Template, Template) ||
                    !P4ExactNativeString(Text(Template), CDO->GetPathName()))))
            return Refuse(TEXT("copy-MIC-cooked-native-template-not-canonical"));
        if (!Node.Undecoded.IsEmpty() || !Node.Properties.Error.Code.empty() || Node.TailStart > Node.TailEnd ||
            Node.TailEnd > File.Body.size() || Node.TailEnd - Node.TailStart != 4 ||
            !CopyMICDecodedFields(Type, Node.Values, 0))
            return Refuse(TEXT("copy-MIC-cooked-frame-unreadable"));
        for (std::size_t Byte = Node.TailStart; Byte < Node.TailEnd; ++Byte)
            if (File.Body[Byte])
                return Refuse(TEXT("copy-MIC-cooked-native-tail-not-canonical"));
        for (const auto& Tag : Node.Properties.Properties)
        {
            const FProperty* P = Type->FindPropertyByName(FName(*Text(Tag.Name)));
            if (!P || !P4ExactNativeString(P->GetName(), Text(Tag.Name)) ||
                Tag.ArrayIndex >= std::size_t(P->ArrayDim) || Tag.HasGuid ||
                Tag.StructGuid != std::array<uint32, 4>{} || Tag.Type != TCHAR_TO_UTF8(*P->GetClass()->GetName()))
                return Refuse(TEXT("copy-MIC-cooked-tag-schema"));
            std::vector<std::string> Details;
            if (const auto* S = CastField<FStructProperty>(P))
                Details.push_back(TCHAR_TO_UTF8(*S->Struct->GetName()));
            else if (const auto* A = CastField<FArrayProperty>(P))
                Details.push_back(TCHAR_TO_UTF8(*A->Inner->GetClass()->GetName()));
            else if (const auto* M = CastField<FMapProperty>(P))
            {
                Details.push_back(TCHAR_TO_UTF8(*M->KeyProp->GetClass()->GetName()));
                Details.push_back(TCHAR_TO_UTF8(*M->ValueProp->GetClass()->GetName()));
            }
            else if (const auto* B = CastField<FByteProperty>(P))
                Details.push_back(B->Enum ? TCHAR_TO_UTF8(*B->Enum->GetName()) : "None");
            else if (const auto* E = CastField<FEnumProperty>(P))
                Details.push_back(TCHAR_TO_UTF8(*E->GetEnum()->GetName()));
            if (Tag.Detail != Details)
                return Refuse(TEXT("copy-MIC-cooked-tag-detail"));
        }
    }
    FString After;
    if (!P4HasIntrinsicGameSource(Asset, &After) || !P4ExactNativeString(Origin, After) || Role->GetOuter() != Asset ||
        P4HasIntrinsicGameProxy(Asset))
        return Refuse(TEXT("copy-MIC-cook-origin-changed"));
    return true;
}

bool CopyMICCookBoundary(UObject* Asset, const UObject* Role, FString& Error)
{
    FFile File;
    return Cooked(Asset, false, File, Error) && CopyMICCookFrame(Asset, Role, File, Error);
}

bool CopyMICBoundaryControls(UObject* Asset, const UObject* Role, FString& Error)
{
    // Test seam over this exact validator's actual cooked input. No live object,
    // original comparator, source record, or file is changed by these negatives.
    FFile Original;
    if (!Cooked(Asset, false, Original, Error) || !CopyMICCookFrame(Asset, Role, Original, Error))
        return false;
    const int32 Root = Original.Nodes.IndexOfByPredicate(
        [&](const FNode& Node)
        {
            return Node.Class == TEXT("/Script/Engine.MaterialInstanceConstant") && Node.Name == Asset->GetName() &&
                !Node.Outer;
        });
    if (Root == INDEX_NONE)
    {
        Error = TEXT("copy-MIC-negative-control-root-missing");
        return false;
    }
    const auto Refused = [&](const FFile& Mutated)
    {
        FString Why;
        return !CopyMICCookFrame(Asset, Role, Mutated, Why) && !Why.IsEmpty();
    };
    FFile Mutated = Original;
    Mutated.Body[Mutated.Nodes[Root].TailStart] = 1;
    if (!Refused(Mutated))
    {
        Error = TEXT("copy-MIC-nonzero-native-tail-control-not-refused");
        return false;
    }
    Mutated = Original;
    --Mutated.Nodes[Root].TailEnd;
    if (!Refused(Mutated))
    {
        Error = TEXT("copy-MIC-truncated-native-tail-control-not-refused");
        return false;
    }
    Mutated = Original;
    Mutated.Nodes[Root].Values.Fields.emplace_back(
        "UnrecognizedCopiedMICField", FDecodedValue::Atom(FDecodedValue::EKind::Number, "7"));
    if (!Refused(Mutated))
    {
        Error = TEXT("copy-MIC-unknown-decoded-field-control-not-refused");
        return false;
    }
    Mutated = Original;
    Mutated.Nodes.Add(Original.Nodes[Root]);
    if (!Refused(Mutated))
    {
        Error = TEXT("copy-MIC-extra-export-control-not-refused");
        return false;
    }
    Mutated = Original;
    if (Mutated.Nodes[Root].Properties.Properties.empty())
    {
        Error = TEXT("copy-MIC-tag-schema-control-not-available");
        return false;
    }
    Mutated.Nodes[Root].Properties.Properties[0].Type = "UnknownProperty";
    if (!Refused(Mutated))
    {
        Error = TEXT("copy-MIC-unknown-native-tag-schema-control-not-refused");
        return false;
    }
    Mutated = Original;
    Mutated.Nodes[Root].Template = Root + 1;
    if (!Refused(Mutated))
    {
        Error = TEXT("copy-MIC-custom-native-template-control-not-refused");
        return false;
    }
    Mutated = Original;
    if (Mutated.Nodes[Root].Values.Fields.empty())
    {
        Error = TEXT("copy-MIC-decoded-kind-control-not-available");
        return false;
    }
    Mutated.Nodes[Root].Values.Fields[0].second = FDecodedValue::Atom(FDecodedValue::EKind::FieldPath, "unrecognized");
    if (!Refused(Mutated))
    {
        Error = TEXT("copy-MIC-unknown-decoded-kind-control-not-refused");
        return false;
    }
    Error.Reset();
    return true;
}
} // namespace P4LevelContent

bool P4MaterialInstanceCopyCookBoundary(UObject* Asset, const UObject* ValidatedRole, FString& Error)
{
    return P4LevelContent::CopyMICCookBoundary(Asset, ValidatedRole, Error);
}

bool P4MaterialInstanceCopyBoundaryControls(UObject* Asset, const UObject* ValidatedRole, FString& Error)
{
    return P4LevelContent::CopyMICBoundaryControls(Asset, ValidatedRole, Error);
}
