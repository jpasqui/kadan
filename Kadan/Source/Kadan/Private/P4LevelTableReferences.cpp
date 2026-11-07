// The Blueprint classes a table's rows name, read with the rows' native types.

#include "P4LevelContentInternal.h"
#include "P4GamePaths.h"

namespace P4LevelContent
{
// A table can name a declared class before that Blueprint's values are imported.
// Inspect used values with their native types; unused package imports prove nothing.
struct FTableDeclarationReferences
{
    const FFile& File;
    const TSet<FName>& Targets;
    TArray<FP4OriginalTableClassReference> Found;
    TSet<FName> Used;
    FString Error;
    bool DiscoverUsedClasses = false;

    bool Refuse(const FString& Where, const FString& Why)
    {
        Error = TEXT("table-declaration-reference: ") + Where + TEXT(": ") + Why;
        return false;
    }

    bool NamesTarget(const FDecodedValue& Said, int32 Depth) const
    {
        if (Depth > 32)
            return true;
        if (Said.Kind == FDecodedValue::EKind::String || Said.Kind == FDecodedValue::EKind::HardReference)
        {
            const FString Path = Text(Said.Text);
            return Path.StartsWith(TEXT("/")) && Targets.Contains(FName(*FPackageName::ObjectPathToPackageName(Path)));
        }
        for (const auto& Child : Said.Fields)
            if (NamesTarget(Child.second, Depth + 1))
                return true;
        for (const auto& Item : Said.Items)
            if (NamesTarget(Item, Depth + 1))
                return true;
        return false;
    }
    bool Members(const UStruct* Type, const FDecodedValue& Said, const FString& Where, int32 Depth)
    {
        if (!Type || Said.Kind != FDecodedValue::EKind::Object || Depth > 32)
            return Refuse(Where, TEXT("row or struct schema is not readable"));
        for (const auto& Child : Said.Fields)
        {
            // Discovery does not turn unrelated or unused import-map entries into
            // declaration work. The complete selected-target proof runs afterward.
            if (DiscoverUsedClasses && !NamesTarget(Child.second, 0))
                continue;
            FString Name = Text(Child.first);
            int32 Slot = 0, Bracket = INDEX_NONE;
            if (Name.FindChar(TEXT('['), Bracket))
            {
                const FString Number = Name.Mid(Bracket + 1, Name.Len() - Bracket - 2);
                Slot = FCString::Atoi(*Number);
                if (!Name.EndsWith(TEXT("]")) || Number != FString::FromInt(Slot))
                    return Refuse(Where + TEXT(".") + Text(Child.first), TEXT("invalid fixed-array member"));
                Name.LeftInline(Bracket);
            }
            const FProperty* Property = Type->FindPropertyByName(FName(*Name));
            const FString Field = Where + TEXT(".") + Text(Child.first);
            // A member the game's engine has and this one lacks (FAnimNotifyEvent's MiddleLink and
            // MiddleTriggerTimeOffset, for two) cannot hold a class here. It is passed over when nothing
            // in its value names one of the Blueprint packages asked about.
            if (!Property && !NamesTarget(Child.second, 0))
                continue;
            if (!Property || Slot < 0 || Slot >= Property->ArrayDim)
                return Refuse(Field, TEXT("native property schema is unavailable"));
            if (Property->HasAnyPropertyFlags(CPF_Transient))
                return Refuse(Field, TEXT("a captured transient member cannot establish an import dependency"));
            if (!Value(Property, Child.second, Field, Depth + 1))
                return false;
        }
        return true;
    }

    bool Value(const FProperty* Property, const FDecodedValue& Said, const FString& Where, int32 Depth)
    {
        using V = FDecodedValue;
        if (!Property || Depth > 32)
            return Refuse(Where, TEXT("property nesting is not readable"));
        if (CastField<FSoftObjectProperty>(Property))
            return true; // A soft path needs no class declaration.
        if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
        {
            // Row values are read typed: a reference is a hard reference with its import index (0 is none).
            if (Said.Kind == V::EKind::Null || (Said.Kind == V::EKind::HardReference && Said.ReferenceIndex == 0))
                return true;
            if (Said.Kind != V::EKind::String && Said.Kind != V::EKind::HardReference)
                return Refuse(Where, TEXT("object reference was not decoded"));
            const FString Path = Text(Said.Text);
            const FName Package(*FPackageName::ObjectPathToPackageName(Path));
            if (!Targets.Contains(Package))
                return true;
            const FClassProperty* Class = CastField<FClassProperty>(Object);
            if (DiscoverUsedClasses && !Class)
                return true;
            if (!Class || !Class->MetaClass)
                return Refuse(
                    Where, TEXT("used reference requires a completed object, not only a class declaration: ") + Path);
            if (DiscoverUsedClasses &&
                (!Property->GetOwnerStruct() ||
                    !Property->GetOwnerStruct()->GetOutermost()->GetName().StartsWith(
                        TEXT("/Script/"), ESearchCase::CaseSensitive)))
                return Refuse(Where, TEXT("used class declaration field is not owned by a native schema"));
            bool Matched = false;
            for (std::size_t Index = 0; Index < File.Package.Imports.size(); ++Index)
            {
                std::string ImportPath;
                if (!File.Package.Reference(-int32(Index) - 1, ImportPath) || FName(*Text(ImportPath)) != FName(*Path))
                    continue;
                const auto& Import = File.Package.Imports[Index];
                const bool IsClass =
                    (Import.ClassPackage == "/Script/Engine" &&
                        (Import.Class == "BlueprintGeneratedClass" || Import.Class == "AnimBlueprintGeneratedClass")) ||
                    (Import.ClassPackage == "/Script/UMG" && Import.Class == "WidgetBlueprintGeneratedClass") ||
                    (Import.ClassPackage == "/Script/CoreUObject" && Import.Class == "Class");
                if (Matched || !IsClass)
                    return Refuse(
                        Where, TEXT("used class import is ambiguous or is not a generated/native class: ") + Path);
                Matched = true;
            }
            if (!Matched)
                return Refuse(Where, TEXT("used class has no matching generated/native class import: ") + Path);
            FP4OriginalTableClassReference Reference;
            Reference.Field = Where;
            Reference.ObjectPath = Path;
            Reference.MetaClassPath = Class->MetaClass->GetPathName();
            Found.Add(MoveTemp(Reference));
            Used.Add(Package);
            return true;
        }
        if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            if (!Struct->Struct)
                return Refuse(Where, TEXT("native struct schema is unavailable"));
            const FString Kind = Struct->Struct->GetPathName();
            if (Kind == TEXT("/Script/CoreUObject.SoftObjectPath") || Kind == TEXT("/Script/CoreUObject.SoftClassPath"))
                return true;
            if (Kind == TEXT("/Script/GameplayTags.GameplayTag") && Said.Kind == V::EKind::Object &&
                Said.Fields.size() == 1 && Said.Field("tag") && Said.Field("tag")->Kind == V::EKind::String)
                return true;
            if (Kind.EndsWith(TEXT(".NavAgentSelector")) && Said.Kind == V::EKind::Object &&
                Struct->Struct->GetStructureSize() == 4 && Said.Fields.size() == 1 && Said.Field("PackedBits") &&
                Said.Field("PackedBits")->Kind == V::EKind::Number)
                return true;
            return Members(Struct->Struct, Said, Where, Depth + 1);
        }
        if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            if (Said.Kind != V::EKind::Array)
                return Refuse(Where, TEXT("array values were not decoded"));
            for (std::size_t Index = 0; Index < Said.Items.size(); ++Index)
                if (!Value(Array->Inner, Said.Items[Index], Where + FString::Printf(TEXT("[%d]"), int32(Index)),
                        Depth + 1))
                    return false;
        }
        else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
        {
            const V* Pairs = Said.Kind == V::EKind::Object ? Said.Field("__native_map_pairs") : nullptr;
            if (!Pairs || Pairs->Kind != V::EKind::Array)
                return Refuse(Where, TEXT("map values were not decoded"));
            for (std::size_t Index = 0; Index < Pairs->Items.size(); ++Index)
            {
                const V& Pair = Pairs->Items[Index];
                const FString At = Where + FString::Printf(TEXT("[%d]"), int32(Index));
                if (Pair.Kind != V::EKind::Array || Pair.Items.size() != 2)
                    return Refuse(At, TEXT("map pair was not decoded"));
                if (!Value(Map->KeyProp, Pair.Items[0], At + TEXT(".Key"), Depth + 1) ||
                    !Value(Map->ValueProp, Pair.Items[1], At + TEXT(".Value"), Depth + 1))
                    return false;
            }
        }
        else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
        {
            if (Said.Kind != V::EKind::Array)
                return Refuse(Where, TEXT("set values were not decoded"));
            for (std::size_t Index = 0; Index < Said.Items.size(); ++Index)
                if (!Value(Set->ElementProp, Said.Items[Index], Where + FString::Printf(TEXT("[%d]"), int32(Index)),
                        Depth + 1))
                    return false;
        }
        else if (!(CastField<FBoolProperty>(Property) || CastField<FNumericProperty>(Property) ||
                     CastField<FEnumProperty>(Property) || CastField<FNameProperty>(Property) ||
                     CastField<FStrProperty>(Property) || CastField<FTextProperty>(Property)))
            return Refuse(
                Where, TEXT("native property reference semantics are unsupported: ") + Property->GetClass()->GetName());
        return true;
    }
};
} // namespace P4LevelContent

static bool FindOriginalTableDeclarationReferences(const FString& HeaderFile, const FString& BodyFile,
    const TSet<FName>& RequestedTargets, TArray<FP4OriginalTableClassReference>& References, FString& Error,
    bool Discover)
{
    using namespace P4LevelContent;
    References.Reset();
    Error.Reset();
    if (!IsInGameThread() || (!Discover && !RequestedTargets.Num()))
    {
        Error = TEXT("Table declaration references require editor-thread native reflection and requested packages.");
        return false;
    }
    const FString BodyPath = BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile;
    TArray<uint8> Header, Body;
    for (const FString& Path : {HeaderFile, BodyPath})
        if (IFileManager::Get().FileSize(*Path) <= 0 || IFileManager::Get().FileSize(*Path) > 64 * 1024 * 1024)
        {
            Error = TEXT("Table declaration source is absent or exceeds the bounded reader: ") + Path;
            return false;
        }
    if (!FFileHelper::LoadFileToArray(Header, *HeaderFile, FILEREAD_Silent) ||
        !FFileHelper::LoadFileToArray(Body, *BodyPath, FILEREAD_Silent))
    {
        Error = TEXT("Table declaration source could not be read.");
        return false;
    }
    FFile File;
    File.Header.assign(Header.GetData(), Header.GetData() + Header.Num());
    File.Body.assign(Body.GetData(), Body.GetData() + Body.Num());
    if (!ReadNodes(File, FString(), Error))
        return false;
    bool HasTable = false;
    for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
        HasTable |= File.Node(Index).Class.EndsWith(TEXT(".DataTable")) ||
            File.Node(Index).Class.EndsWith(TEXT(".CompositeDataTable"));
    if (Discover && !HasTable)
    {
        Error = TEXT("Declaration discovery requires an actual native DataTable source.");
        return false;
    }
    TSet<FName> TargetPackages = RequestedTargets;
    if (Discover)
    {
        // These are candidates only. An unused import, CDO/object property, soft
        // class path or name ending in _C never authorizes Blueprint hydration.
        for (std::size_t Index = 0; Index < File.Package.Imports.size(); ++Index)
        {
            const auto& Import = File.Package.Imports[Index];
            const bool Generated =
                (Import.ClassPackage == "/Script/Engine" &&
                    (Import.Class == "BlueprintGeneratedClass" || Import.Class == "AnimBlueprintGeneratedClass")) ||
                (Import.ClassPackage == "/Script/UMG" && Import.Class == "WidgetBlueprintGeneratedClass");
            if (!Generated)
                continue;
            std::string Path;
            if (!File.Package.Reference(-int32(Index) - 1, Path))
            {
                Error = TEXT("Table declaration candidate import identity is unreadable.");
                return false;
            }
            const FString Package = FPackageName::ObjectPathToPackageName(Text(Path));
            if (P4IsValidContentPackage(Package))
                TargetPackages.Add(FName(*Package));
        }
        // A class-free table remains on its ordinary native importer path even
        // when its row payload is unsupported. This query adds no new refusal.
        if (!TargetPackages.Num())
            return true;
    }
    if (!ReadTables(File, true, Error, true))
        return false;
    if (HasTable && !File.Tables.Num())
    {
        Error = TEXT("Table declaration source has no completely decoded table rows.");
        return false;
    }
    const auto Collect = [&](FTableDeclarationReferences& Query) -> bool
    {
        for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
        {
            const FNode& Node = File.Node(Index);
            if (!Node.Undecoded.IsEmpty())
            {
                Error = TEXT("Table declaration object properties were not decoded: ") + Node.Path;
                return false;
            }
            if (!Query.DiscoverUsedClasses &&
                Query.Targets.Contains(FName(*FPackageName::ObjectPathToPackageName(Node.Class))))
            {
                Error =
                    TEXT("Table declaration source contains an instance that requires completed Blueprint values: ") +
                    Node.Path;
                return false;
            }
            UClass* Type = LoadObject<UClass>(nullptr, *Node.Class, nullptr, LOAD_NoWarn | LOAD_Quiet);
            if (!Query.Members(Type, Node.Values, Node.Path, 0))
            {
                Error = Query.Error;
                return false;
            }
            const FTableRows* Rows = File.Tables.Find(Index);
            if (!Rows)
                continue;
            const FDecodedValue* RowType = Node.Values.Field("RowStruct");
            UScriptStruct* Struct = RowType && RowType->Kind == FDecodedValue::EKind::String
                ? LoadObject<UScriptStruct>(nullptr, *Text(RowType->Text), nullptr, LOAD_NoWarn | LOAD_Quiet)
                : nullptr;
            for (int32 Row = 0; Row < Rows->Rows.Num(); ++Row)
                if (!Query.Members(Struct, Rows->Rows[Row], Node.Path + TEXT(".") + Rows->Names[Row], 0))
                {
                    Error = Query.Error;
                    return false;
                }
        }
        return true;
    };
    if (Discover)
    {
        FTableDeclarationReferences Discovery{File, TargetPackages};
        Discovery.DiscoverUsedClasses = true;
        if (!Collect(Discovery))
            return false;
        // Re-run the original full proof for the actually used target set. In
        // particular, a same-package instance/CDO reference cannot be deferred.
        TargetPackages = MoveTemp(Discovery.Used);
        if (!TargetPackages.Num())
            return true;
    }
    FTableDeclarationReferences Query{File, TargetPackages};
    if (!Collect(Query))
        return false;
    for (const FName& Target : TargetPackages)
        if (!Query.Used.Contains(Target))
        {
            Error = TEXT("Table dependency has no used native class-typed reference: ") + Target.ToString();
            return false;
        }
    References = MoveTemp(Query.Found);
    return true;
}

bool P4OriginalTableDeclarationReferences(const FString& HeaderFile, const FString& BodyFile,
    const TSet<FName>& TargetPackages, TArray<FP4OriginalTableClassReference>& References, FString& Error)
{
    return FindOriginalTableDeclarationReferences(HeaderFile, BodyFile, TargetPackages, References, Error, false);
}

bool P4OriginalTableUsedClassReferences(const FString& HeaderFile, const FString& BodyFile,
    TArray<FP4OriginalTableClassReference>& References, FString& Error)
{
    return FindOriginalTableDeclarationReferences(HeaderFile, BodyFile, {}, References, Error, true);
}
