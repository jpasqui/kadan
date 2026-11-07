// What the editor changes in a game level when it opens or saves one, told apart from what the level
// says.

#include "P4LevelContentInternal.h"
#include "P4Common.h"

namespace P4LevelContent
{
// When a map opens, the editor makes a navigation mesh actor for every kind of creature the project
// knows that the map has none for. The game's map has those the game gave it and no others;
// export keeps the game's stock navigation, so such an actor is the editor's own:
// it is not something the person did and not something a mod carries. What a level says is told
// without the navigation data it did not have when it was imported, and their names are given.
void WithoutEditorNavigation(UWorld* World, TArray<FString>& Content, TArray<FString>* Made)
{
    TArray<FString> Imported;
    if (!World->GetOutermost()->GetMetaData()->HasValue(World, ImportedKey) ||
        !Unpack(World->GetOutermost()->GetMetaData()->GetValue(World, ImportedKey), Imported))
        return;
    const FString Level = World->GetName() + TEXT(".") + World->PersistentLevel->GetName();
    TSet<FString, FLocKeySetFuncs> Had;
    for (const FString& Line : Imported)
    {
        const FString Name = OwnerOf(Line, Level);
        if (!Name.IsEmpty())
            Had.Add(Name);
    }
    TSet<FString, FLocKeySetFuncs> Names;
    for (const AActor* Actor : World->PersistentLevel->Actors)
        if (Actor && !Actor->IsPendingKill() && Actor->IsA<ANavigationData>() && !Had.Contains(Actor->GetName()))
            Names.Add(Actor->GetName());
    if (!Names.Num())
        return;
    const FString ListStart = Level + TEXT(" | actors ");
    for (int32 Index = Content.Num(); Index-- > 0;)
    {
        FString& Line = Content[Index];
        if (Line.StartsWith(ListStart, ESearchCase::CaseSensitive))
        {
            TArray<FString> Listed;
            Line.Mid(ListStart.Len()).ParseIntoArray(Listed, TEXT(", "), true);
            Listed.RemoveAll(
                [&](const FString& Path)
                {
                    return Path.StartsWith(Level + TEXT("."), ESearchCase::CaseSensitive) &&
                        Names.Contains(Path.Mid(Level.Len() + 1));
                });
            Line = ListStart + FString::Join(Listed, TEXT(", "));
        }
        else if (Names.Contains(OwnerOf(Line, Level)))
            Content.RemoveAt(Index);
    }
    if (Made)
    {
        *Made = Names.Array();
        Made->Sort();
    }
}

// When a map opens, the editor removes a streaming entry whose level package is not there. A game
// map can name a level the game does not ship (a test or bake level). The game loads nothing for it
// and the editor drops it on opening, so the entry is the game's and not a change the person made:
// its imported lines are told again, and the level's list of streaming levels is the imported one.
void WithEditorDroppedStreams(UWorld* World, TArray<FString>& Content, TArray<FString>* Dropped)
{
    TArray<FString> Imported;
    if (!World->GetOutermost()->GetMetaData()->HasValue(World, ImportedKey) ||
        !Unpack(World->GetOutermost()->GetMetaData()->GetValue(World, ImportedKey), Imported))
        return;
    const FString Owner = World->GetName();
    TSet<FString> Present;
    for (const ULevelStreaming* Stream : World->GetStreamingLevels())
        if (Stream)
            Present.Add(Stream->GetName());
    TSet<FString> Gone;
    const FString AssetSays = TEXT(" | worldasset = \"");
    for (const FString& Line : Imported)
    {
        if (!Line.StartsWith(Owner + TEXT("."), ESearchCase::CaseSensitive))
            continue;
        const int32 At = Line.Find(AssetSays, ESearchCase::CaseSensitive);
        if (At == INDEX_NONE)
            continue;
        const FString Name = Line.Mid(Owner.Len() + 1, At - Owner.Len() - 1);
        if (Name.Contains(TEXT(".")) || Name.Contains(TEXT(" ")) || Present.Contains(Name))
            continue;
        FString Path = Line.Mid(At + AssetSays.Len());
        Path.RemoveFromEnd(TEXT("\""));
        const FString Package = FPackageName::ObjectPathToPackageName(Path);
        if (Package.IsEmpty() || !FPackageName::IsValidLongPackageName(Package) ||
            FPackageName::DoesPackageExist(Package))
            continue;
        Gone.Add(Name);
    }
    if (!Gone.Num())
        return;
    for (const FString& Line : Content)
        for (const FString& Name : Gone)
            if (Line.StartsWith(Owner + TEXT(".") + Name + TEXT(" |"), ESearchCase::CaseSensitive))
                return; // still there: not the editor's drop
    const FString ListStart = Owner + TEXT(" | streaming levels ");
    const auto Listed = [&](const FString& Line)
    {
        TArray<FString> Out;
        Line.Mid(ListStart.Len()).ParseIntoArray(Out, TEXT(", "), true);
        Out.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
        return Out;
    };
    const FString* Was = Imported.FindByPredicate(
        [&](const FString& Line) { return Line.StartsWith(ListStart, ESearchCase::CaseSensitive); });
    const int32 NowAt = Content.IndexOfByPredicate(
        [&](const FString& Line) { return Line.StartsWith(ListStart, ESearchCase::CaseSensitive); });
    if (Was)
    {
        TArray<FString> Expected = Listed(*Was);
        Expected.RemoveAll(
            [&](const FString& Path)
            {
                return Path.StartsWith(Owner + TEXT("."), ESearchCase::CaseSensitive) &&
                    Gone.Contains(Path.Mid(Owner.Len() + 1));
            });
        TArray<FString> Now;
        if (NowAt != INDEX_NONE)
            Now = Listed(Content[NowAt]);
        if (Now != Expected)
            return; // the person changed the list too: told as it is
        if (NowAt != INDEX_NONE)
            Content[NowAt] = *Was;
        else
            Content.Add(*Was);
    }
    for (const FString& Line : Imported)
        for (const FString& Name : Gone)
            if (Line.StartsWith(Owner + TEXT(".") + Name + TEXT(" |"), ESearchCase::CaseSensitive))
            {
                Content.Add(Line);
                break;
            }
    Content.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
    if (Dropped)
    {
        *Dropped = Gone.Array();
        Dropped->Sort();
    }
}

// A navigation link's user id is given again when the editor registers it and another loaded link
// already has it, as the game does when it loads the levels together. The id the editor gave is its
// own and not a change the person made: the imported id is told.
void WithEditorNavLinkIds(UWorld* World, TArray<FString>& Content)
{
    TArray<FString> Imported;
    if (!World->GetOutermost()->GetMetaData()->HasValue(World, ImportedKey) ||
        !Unpack(World->GetOutermost()->GetMetaData()->GetValue(World, ImportedKey), Imported))
        return;
    const FString Said = TEXT(" | navlinkuserid = ");
    TMap<FString, FString> Was;
    for (const FString& Line : Imported)
    {
        const int32 At = Line.Find(Said, ESearchCase::CaseSensitive);
        if (At != INDEX_NONE)
            Was.Add(Line.Left(At), Line);
    }
    if (!Was.Num())
        return;
    bool Changed = false;
    for (FString& Line : Content)
    {
        const int32 At = Line.Find(Said, ESearchCase::CaseSensitive);
        if (At == INDEX_NONE)
            continue;
        const FString* Imported_ = Was.Find(Line.Left(At));
        if (Imported_ && *Imported_ != Line)
        {
            Line = *Imported_;
            Changed = true;
        }
    }
    if (Changed)
        Content.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
}

// When a map opens, the editor makes a landscape's material instances again from its material and
// weight maps, under new names. Instances that say the same, for the same components, are the
// editor's own and not a change the person made: the imported lines are told.
void WithEditorLandscapeMaterials(UWorld* World, TArray<FString>& Content)
{
    TArray<FString> Imported;
    if (!World->GetOutermost()->GetMetaData()->HasValue(World, ImportedKey) ||
        !Unpack(World->GetOutermost()->GetMetaData()->GetValue(World, ImportedKey), Imported))
        return;
    const FString Kind = TEXT(".LandscapeMaterialInstanceConstant_"), Slots = TEXT(" | materialinstances = ");
    // Owner path of a landscape instance line, or empty.
    const auto Instance = [&](const FString& Line)
    {
        FString Path;
        if (!Line.Split(TEXT(" | "), &Path, nullptr))
            return FString();
        const int32 At = Path.Find(Kind, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        if (At == INDEX_NONE)
            return FString();
        for (int32 Index = At + Kind.Len(); Index < Path.Len(); ++Index)
            if (!FChar::IsDigit(Path[Index]))
                return FString();
        return Path;
    };
    // The lines involved, and a form of them that names each instance by what it says.
    const auto Canonical = [&](const TArray<FString>& Lines, TArray<FString>& Involved)
    {
        TMap<FString, TArray<FString>> Says;
        for (const FString& Line : Lines)
        {
            const FString Path = Instance(Line);
            if (!Path.IsEmpty())
            {
                Says.FindOrAdd(Path).Add(Line.Mid(Path.Len()));
                Involved.Add(Line);
            }
        }
        // An instance names its parent instance, which the editor renames too: names resolve parents first.
        TMap<FString, FString> Named;
        TFunction<FString(const FString&, int32)> Name = [&](const FString& Path, int32 Depth) -> FString
        {
            if (const FString* Done = Named.Find(Path))
                return *Done;
            TArray<FString> Lines_ = Says.FindRef(Path);
            for (FString& Line : Lines_)
                for (const auto& Other : Says)
                    if (Other.Key != Path && Depth < 8 &&
                        Line.Contains(TEXT("\"") + Other.Key + TEXT("\""), ESearchCase::CaseSensitive))
                        Line.ReplaceInline(*(TEXT("\"") + Other.Key + TEXT("\"")), *Name(Other.Key, Depth + 1),
                            ESearchCase::CaseSensitive);
            Lines_.Sort();
            const FString Form = TEXT("{") + FString::Join(Lines_, TEXT(";")) + TEXT("}");
            Named.Add(Path, Form);
            return Form;
        };
        for (const auto& Pair : Says)
            Name(Pair.Key, 0);
        TArray<FString> Out;
        for (const auto& Pair : Named)
            Out.Add(Pair.Value);
        for (const FString& Line : Lines)
            if (Line.Contains(Slots, ESearchCase::CaseSensitive) && Line.Contains(Kind, ESearchCase::CaseSensitive))
            {
                FString Form = Line;
                Involved.Add(Line);
                for (const auto& Pair : Named)
                    Form.ReplaceInline(*(TEXT("\"") + Pair.Key + TEXT("\"")), *Pair.Value, ESearchCase::CaseSensitive);
                Out.Add(Form);
            }
        Out.Sort();
        return Out;
    };
    TArray<FString> WasInvolved, NowInvolved;
    const TArray<FString> Was = Canonical(Imported, WasInvolved), Now = Canonical(Content, NowInvolved);
    if (!WasInvolved.Num() || Was != Now)
        return;
    const TSet<FString> Drop(NowInvolved);
    Content.RemoveAll([&](const FString& Line) { return Drop.Contains(Line); });
    Content.Append(WasInvolved);
    Content.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
}

void WithoutAddedNativeTransformTracking(UWorld* World, TArray<FString>& Content)
{
    TArray<FString> Imported;
    FP4OriginalRecord Record;
    FString Error, Check;
    if (!P4ReadOriginalRecord(World, Record, Error) || Record.Kind != TEXT("level") ||
        !Unpack(World->GetOutermost()->GetMetaData()->GetValue(World, ImportedKey), Imported) ||
        !Digest(Imported, Check) || !P4SameText(Check, Record.ComparisonDigest))
        return;
    const FString Mark = TEXT(" | ucsmodifiedproperties = ");
    const FString Level = World->GetName() + TEXT(".") + World->PersistentLevel->GetName() + TEXT(".");
    bool AnyChanged = false;
    for (int32 Index = Content.Num(); Index-- > 0;)
    {
        FString Path, Json;
        if (!Content[Index].Split(Mark, &Path, &Json, ESearchCase::CaseSensitive) ||
            !Path.StartsWith(Level, ESearchCase::CaseSensitive) ||
            !Imported.ContainsByPredicate([&](const FString& Line)
                { return Line.StartsWith(Path + TEXT(" | class "), ESearchCase::CaseSensitive); }))
            continue;
        FString Below = Path.Mid(Level.Len()), Name;
        int32 Dot = INDEX_NONE;
        if (!Below.FindChar(TEXT('.'), Dot))
            continue;
        Name = Below.Left(Dot);
        Below = Below.Mid(Dot + 1);
        UObject* Target = FindObject<AActor>(World->PersistentLevel, *Name);
        for (int32 Guard = 0; Target && !Below.IsEmpty() && Guard < 8; ++Guard)
        {
            const int32 Child = Below.Find(TEXT(">child"), ESearchCase::CaseSensitive);
            if (Child == INDEX_NONE)
            {
                Target = StaticFindObject(UObject::StaticClass(), Target, *Below);
                Below.Reset();
                break;
            }
            auto* Part = Cast<UChildActorComponent>(
                StaticFindObject(UObject::StaticClass(), Target, *Below.Left(Child)));
            Target = Part ? Part->GetChildActor() : nullptr;
            Below = Below.Mid(Child + 6);
            Below.RemoveFromStart(TEXT("."));
        }
        auto* Part = Cast<UActorComponent>(Target);
        if (!Part || Part->IsTemplate())
            continue;
        FDecodedValue Current, Before;
        if (!P4MapKit::ReadDecodedJson(std::string(TCHAR_TO_UTF8(*Json)), Current) ||
            Current.Kind != FDecodedValue::EKind::Array)
            continue;
        const FString Key = Path + Mark;
        const FString* Previous = Imported.FindByPredicate(
            [&](const FString& Line) { return Line.StartsWith(Key, ESearchCase::CaseSensitive); });
        if (Previous &&
            (!P4MapKit::ReadDecodedJson(std::string(TCHAR_TO_UTF8(*Previous->Mid(Key.Len()))), Before) ||
                Before.Kind != FDecodedValue::EKind::Array))
            continue;
        bool Changed = false;
        for (std::size_t Slot = Current.Items.size(); Slot-- > 0;)
        {
            const auto& Member = Current.Items[Slot];
            if (!P4NativeTransformConstructionTracking(Part->GetClass(), Member))
                continue;
            const std::string Encoded = P4MapKit::DecodedJson(Member);
            if (std::any_of(Before.Items.begin(), Before.Items.end(),
                    [&](const FDecodedValue& Item) { return P4MapKit::DecodedJson(Item) == Encoded; }))
                continue;
            Current.Items.erase(Current.Items.begin() + Slot);
            Changed = true;
        }
        if (Changed)
        {
            AnyChanged = true;
            if (Current.Items.empty() && !Previous)
                Content.RemoveAt(Index);
            else
                Content[Index] = Key + Text(P4MapKit::DecodedJson(Current));
        }
    }
    if (AnyChanged)
        Content.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
}

void WithInheritedAnimationClasses(UWorld* World, TArray<FString>& Content)
{
    TArray<FString> Imported;
    FP4OriginalRecord Record;
    FString Error, DigestOfImported;
    if (!P4ReadOriginalRecord(World, Record, Error) || Record.Kind != TEXT("level") ||
        !Unpack(World->GetOutermost()->GetMetaData()->GetValue(World, ImportedKey), Imported) ||
        !Digest(Imported, DigestOfImported) || !P4SameText(DigestOfImported, Record.ComparisonDigest))
        return;
    const FClassProperty* Field = FindFProperty<FClassProperty>(
        USkeletalMeshComponent::StaticClass(), TEXT("AnimClass"));
    if (!Field || Field->GetOwnerStruct() != USkeletalMeshComponent::StaticClass() || Field->ArrayDim != 1 ||
        Field->MetaClass != UAnimInstance::StaticClass() ||
        Field->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_SkipSerialization))
        return;
    bool Changed = false;
    const FString Mark = TEXT(" | animclass = ");
    for (const FString& Line : Imported)
    {
        FString Path, Former;
        if (!Line.Split(Mark, &Path, &Former, ESearchCase::CaseSensitive))
            continue;
        const FString Key = Path + Mark;
        if (Content.ContainsByPredicate(
                [&](const FString& Current) { return Current.StartsWith(Key, ESearchCase::CaseSensitive); }))
            continue;
        auto* Part = Cast<USkeletalMeshComponent>(
            StaticFindObject(UObject::StaticClass(), World->GetOutermost(), *Path));
        auto* Template = Part ? Cast<USkeletalMeshComponent>(Part->GetArchetype()) : nullptr;
        if (!Part || !Template || Part->GetClass() != Template->GetClass() ||
            !Field->Identical_InContainer(Part, Template))
            continue;
        UClass* Current = Part->AnimClass;
        if (Current &&
            (!IsValid(Current) || !Current->IsChildOf(UAnimInstance::StaticClass()) ||
                !Current->GetPathName().StartsWith(TEXT("/"))))
            continue;
        // Tagged cooking omits a value equal to the current Blueprint template. A
        // repaired template may now supply a class the original level wrote explicitly.
        // Compare the actual typed value, including a creator's different class or null;
        // never put the old value back into the component or waive a real edit.
        FDecodedValue Value = FDecodedValue::Atom(Current ? FDecodedValue::EKind::String : FDecodedValue::EKind::Null,
            Current ? TCHAR_TO_UTF8(*Current->GetPathName()) : "null");
        Content.Add(Key + Text(P4MapKit::DecodedJson(Value)));
        Changed = true;
    }
    if (Changed)
        Content.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
}

void WithInheritedRuleTableDefaults(UWorld* World, TArray<FString>& Content)
{
    TArray<FString> Imported;
    FP4OriginalRecord Record;
    FString Error, Check;
    if (!P4ReadOriginalRecord(World, Record, Error) || Record.Kind != TEXT("level") ||
        !Unpack(World->GetOutermost()->GetMetaData()->GetValue(World, ImportedKey), Imported) ||
        !Digest(Imported, Check) || !P4SameText(Check, Record.ComparisonDigest))
        return;
    P4WithInheritedTemplateAliases(World, Imported, Content);
    // A line is "<path> | <field> = <value>". The fields the current state already writes.
    const FString Separator = TEXT(" | ");
    const FString Equals = TEXT(" = ");
    TSet<FString> Written;
    for (const FString& Now : Content)
    {
        const int32 At = Now.Find(Equals, ESearchCase::CaseSensitive);
        if (At != INDEX_NONE)
            Written.Add(Now.Left(At + Equals.Len()));
    }
    bool Changed = false;
    for (const FString& Line : Imported)
    {
        FString Path, Rest, Name, Previous;
        if (!Line.Split(Separator, &Path, &Rest, ESearchCase::CaseSensitive) ||
            !Rest.Split(Equals, &Name, &Previous, ESearchCase::CaseSensitive))
            continue;
        const FString Key = Path + Separator + Name + Equals;
        if (Written.Contains(Key))
            continue;
        // Any actor field holding data tables, by its reflected type: an array of data table references.
        auto* Actor = Cast<AActor>(StaticFindObject(UObject::StaticClass(), World->GetOutermost(), *Path));
        auto* Template = Actor ? Cast<AActor>(Actor->GetArchetype()) : nullptr;
        const auto* Field = Actor ? FindFProperty<FArrayProperty>(Actor->GetClass(), *Name) : nullptr;
        const auto* Inner = Field ? CastField<FObjectProperty>(Field->Inner) : nullptr;
        const auto* Owner = Field ? Cast<UClass>(Field->GetOwnerStruct()) : nullptr;
        if (!Actor || Actor->GetLevel() != World->PersistentLevel || Actor->IsTemplate() || !Template ||
            Actor->GetClass() != Template->GetClass() || !Field || Field->ArrayDim != 1 ||
            Field->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_SkipSerialization) || !Owner ||
            !Actor->GetClass()->IsChildOf(Owner) || !Inner || !Inner->PropertyClass ||
            !Inner->PropertyClass->IsChildOf(UDataTable::StaticClass()) ||
            !Field->Identical_InContainer(Actor, Template))
            continue;
        FScriptArrayHelper Values(Field, Field->ContainerPtrToValuePtr<void>(Actor));
        if (Values.Num() > 256)
            continue;
        FDecodedValue Actual;
        Actual.Kind = FDecodedValue::EKind::Array;
        bool Valid = true;
        for (int32 Index = 0; Index < Values.Num(); ++Index)
        {
            UObject* Value = Inner->GetObjectPropertyValue(Values.GetRawPtr(Index));
            if (Value && (!IsValid(Value) || !Value->IsA<UDataTable>() || !Value->GetPathName().StartsWith(TEXT("/"))))
            {
                Valid = false;
                break;
            }
            Actual.Items.push_back(
                FDecodedValue::Atom(Value ? FDecodedValue::EKind::String : FDecodedValue::EKind::Null,
                    Value ? TCHAR_TO_UTF8(*Value->GetPathName()) : "null"));
        }
        // A repaired class now supplies the same typed array. Its absent cook
        // tag is represented by that actual value, never by the former text.
        if (Valid)
        {
            Content.Add(Key + Text(P4MapKit::DecodedJson(Actual)));
            Written.Add(Key);
            Changed = true;
        }
    }
    if (Changed)
        Content.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
}
} // namespace P4LevelContent
