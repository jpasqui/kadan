// Giving the objects made for a level or an asset the values the game's file says.

#include "P4LevelContentInternal.h"
#include "P4Common.h"
#include "P4GamePaths.h"

namespace P4LevelContent
{
bool GiveDelegate(const FProperty* Property, UObject* Live, int32 Place, const FDecodedValue& Said,
    TFunctionRef<UObject*(const FString&)> Resolve)
{
    const FDelegateProperty* One = CastField<FDelegateProperty>(Property);
    const FMulticastDelegateProperty* Many = CastField<FMulticastDelegateProperty>(Property);
    const auto Bound = [&](const FDecodedValue& Binding, FScriptDelegate& Out)
    {
        const FDecodedValue* Object = Binding.Field("object");
        const FDecodedValue* Function = Binding.Field("function");
        if (!Object || !Function)
            return false;
        if (Object->Kind == FDecodedValue::EKind::Null)
            return true;
        UObject* Target = Resolve(Text(Object->Text));
        if (!Target)
            return false;
        Out.BindUFunction(Target, FName(*Text(Function->Text)));
        return true;
    };
    void* Value = Property->ContainerPtrToValuePtr<void>(Live, Place);
    if (Many && Said.Kind == FDecodedValue::EKind::Array)
    {
        bool Held = true;
        Many->ClearDelegate(Live, Value);
        for (const FDecodedValue& Binding : Said.Items)
        {
            FScriptDelegate Delegate;
            if (Bound(Binding, Delegate))
                Many->AddDelegate(MoveTemp(Delegate), Live, Value);
            else
                Held = false;
        }
        return Held;
    }
    return One && Bound(Said, *static_cast<FScriptDelegate*>(Value));
}

// The export's declared template, not whichever fallback the editor constructor found before the
// original Blueprint templates were available. A file stores only deviations from this object.
KADAN_API UObject* ResolveOriginalExternalTemplate(const FString& Reference)
{
    int32 Dot = INDEX_NONE;
    if (!Reference.FindChar(TEXT('.'), Dot))
        return nullptr;
    // Compiled-in /Script packages have no disk linker; LoadPackage may return null even
    // though the authoritative package and its native classes are already registered.
    UPackage* Package = FindPackage(nullptr, *Reference.Left(Dot));
    if (!Package)
        Package = LoadPackage(nullptr, *Reference.Left(Dot), LOAD_NoWarn | LOAD_Quiet);
    if (!Package)
        return nullptr;
    const FString ObjectPath = Reference.Mid(Dot + 1);
    UObject* Template = StaticFindObject(UObject::StaticClass(), Package, *ObjectPath);
    if (Template)
        return Template;
    // Native CDOs are lazy. Loading /Script/Engine or its UClass need not materialize
    // Default__ArrowComponent; require the exact declared native identity before doing so.
    // Never create a missing Blueprint override or a guessed nested subobject.
    FString RootName = ObjectPath;
    int32 ChildDot = INDEX_NONE;
    if (RootName.FindChar(TEXT('.'), ChildDot))
        RootName.LeftInline(ChildDot);
    if (!Reference.StartsWith(TEXT("/Script/")) || !RootName.StartsWith(TEXT("Default__")))
        return nullptr;
    UClass* Class = FindObject<UClass>(Package, *RootName.Mid(9));
    if (!Class || !Class->HasAnyClassFlags(CLASS_Native) || Class->GetOutermost() != Package)
        return nullptr;
    UObject* Default = Class->GetDefaultObject();
    if (!Default || Default->GetFName() != FName(*RootName) || Default->GetOutermost() != Package)
        return nullptr;
    return StaticFindObject(UObject::StaticClass(), Package, *ObjectPath);
}

UObject* DeclaredTemplate(
    const FFile& File, int32 Index, const TMap<int32, UObject*>& Made, UObject* Fallback, FString& Error)
{
    const FNode& Node = File.Node(Index);
    if (!Node.Template)
        return Fallback;
    if (File.Export(Node.Template))
    {
        if (UObject* const* Found = Made.Find(Node.Template))
            return *Found;
        Error = TEXT("level-import-template-not-made-yet: ") + File.Ref(Node.Template);
        return nullptr;
    }
    const FString Reference = File.Ref(Node.Template);
    UObject* Template = ResolveOriginalExternalTemplate(Reference);
    if (Template)
        return Template;
    // Legacy imported Blueprints may have lost a redundant inherited override. Only the existing
    // source-file proof of identical parent templates permits that fallback; nonempty missing
    // templates must refuse reconstruction, never silently supply native defaults.
    if (Fallback)
    {
        FString Actual = Fallback->GetPathName();
        Actual.ReplaceInline(TEXT(":"), TEXT("."));
        TMap<FString, FString> Known;
        if (SameInEffect(File.SourceRoot, Reference, Actual, Known))
            return Fallback;
    }
    Error = TEXT("level-import-template-not-in-the-project: ") + Reference;
    return nullptr;
}

// Puts a value back to what the object's archetype has: what a file that does not say the value means.
// Left alone is what is the engine's to keep: values it does not save, the object's own parts, and
// references to anything but an asset.
void Reset(const FProperty* Property, void* Live, const void* Archetype, int32 Depth, UObject* InstanceOwner,
    const UObject* TemplateOwner)
{
    if (Depth > 16 ||
        Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient |
            CPF_EditorOnly | CPF_SkipSerialization))
        return;
    const bool Instanced = Property->HasAnyPropertyFlags(
        CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_PersistentInstance);
    // Only a direct hard object slot can be mapped to an already reconstructed source child.
    // Containers and structs containing instanced references still retain their constructor state.
    if (Instanced &&
        (!CastField<FObjectProperty>(Property) || !InstanceOwner || !TemplateOwner ||
            !Property->HasAnyPropertyFlags(CPF_InstancedReference)))
        return;
    if (CastField<FDelegateProperty>(Property) || CastField<FMulticastDelegateProperty>(Property) ||
        CastField<FLazyObjectProperty>(Property) || CastField<FInterfaceProperty>(Property))
        return;
    if (const FObjectPropertyBase* Reference = CastField<FObjectPropertyBase>(Property))
    {
        if (CastField<FSoftObjectProperty>(Property))
        {
            Property->CopySingleValue(Live, Archetype);
            return;
        }
        // Nothing, or an asset: the same for every object of the class. Anything else is a part of
        // the archetype itself, and the object has its own in its place.
        const UObject* Value = Reference->GetObjectPropertyValue(Archetype);
        const UObject* Mine = Reference->GetObjectPropertyValue(Live);
        if (Value && InstanceOwner && TemplateOwner && Value->GetOuter() == TemplateOwner &&
            Property->HasAnyPropertyFlags(CPF_InstancedReference))
        {
            // A source child absent from the constructor is made by Inside. Its original owner
            // property must point at that instance, not a leftover differently named parent child.
            if (UObject* Child = StaticFindObjectFast(nullptr, InstanceOwner, Value->GetFName()))
                if (Child->GetClass() == Value->GetClass())
                    Reference->SetObjectPropertyValue(Live, Child);
            return;
        }
        if (Instanced)
            return; // Never copy a template child pointer or clear a constructor-owned slot.
        // A reference of the object's to something in its level (the actor a component made, what a
        // component is attached to) is the engine's to keep until the file says otherwise.
        if (!Value && Mine && !Mine->IsAsset() && !Mine->IsA<UField>())
            return;
        if (!Value || Value->IsAsset() || Value->IsA<UField>())
            Reference->SetObjectPropertyValue(Live, const_cast<UObject*>(Value));
        return;
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        for (TFieldIterator<FProperty> Member(Struct->Struct); Member; ++Member)
            for (int32 Place = 0; Place < Member->ArrayDim; ++Place)
                Reset(*Member, Member->ContainerPtrToValuePtr<void>(Live, Place),
                    Member->ContainerPtrToValuePtr<void>(Archetype, Place), Depth + 1, InstanceOwner, TemplateOwner);
        return;
    }
    if (!Property->Identical(Live, Archetype, PPF_None))
        Property->CopySingleValue(Live, Archetype);
}

// Makes an object what a load of the game's file would make it: what its archetype has, and on top of
// that what the file says. Final says that everything is made.
bool Apply(const FFile& File, int32 Index, UObject* Live, const TMap<int32, UObject*>& Made, bool Final, FString& Error,
    TArray<FString>* NotHeld, TArray<FString>* NotShipped, bool MissingAsNotHeld)
{
    const FNode& Node = File.Node(Index);
    UClass* Class = Live->GetClass();
    bool Complete = true;
    // A class default object's archetype is its parent class's default object: what the file says of it
    // is said over the parent's values, for the properties the parent class has.
    const UObject* Archetype = File.World ? DeclaredTemplate(File, Index, Made, Live->GetArchetype(), Error)
                                          : Live->GetArchetype();
    if (File.World && !Archetype)
        return false;
    if (Archetype && !Class->IsChildOf(Archetype->GetClass()))
    {
        Error = TEXT("level-import-template-class-differs: ") + Node.Path;
        return false;
    }
    if (Archetype)
        if (Archetype != Live && Class->IsChildOf(Archetype->GetClass()))
            for (TFieldIterator<FProperty> Field(Archetype->GetClass()); Field; ++Field)
                for (int32 Place = 0; Place < Field->ArrayDim; ++Place)
                    Reset(*Field, Field->ContainerPtrToValuePtr<void>(Live, Place),
                        Field->ContainerPtrToValuePtr<void>(Archetype, Place), 0, Live, Archetype);
    for (const auto& Field : Node.Values.Fields)
    {
        // A fixed list's later places are said as Name[1], Name[2].
        FString Key = Text(Field.first);
        int32 Place = 0, Bracket = INDEX_NONE;
        if (Key.FindChar(TEXT('['), Bracket))
        {
            Place = FCString::Atoi(*Key.Mid(Bracket + 1));
            Key.LeftInline(Bracket);
        }
        const FProperty* Property = Class->FindPropertyByName(FName(*Key));
        const FString Where = Node.Path + TEXT(" ") + Text(Field.first);
        // A property the game's engine has and this engine does not is passed over and told, when the
        // caller keeps what is told.
        if (!Property && P4CanStoreStockEngineField(Live, FName(*Key)))
        {
            if (Key == TEXT("C4CollisionMaterial") && Field.second.Kind == FDecodedValue::EKind::String &&
                Field.second.Text == Unshipped)
            {
                // The same explicit absent-game-export rule as the normal object writer.
                if (NotShipped)
                    NotShipped->AddUnique(Where);
                continue;
            }
            if (Place != 0 || !P4StoreDecodedStockEngineField(Live, Key, Field.second, Error))
                return false;
            continue;
        }
        if (!Property && NotHeld)
        {
            NotHeld->AddUnique(Where + TEXT(" = ") + Text(P4MapKit::DecodedJson(Field.second)));
            continue;
        }
        if (!Property || Place < 0 || Place >= Property->ArrayDim)
        {
            Error = TEXT("level-import-property-not-in-this-engine: ") + Where + TEXT(" (") + Node.Class + TEXT(")");
            return false;
        }
        // What a delegate calls, as the game's editor saved it (a Blueprint's bound component events): each binding's
        // object, one of this file's, and function name.
        if (CastField<FDelegateProperty>(Property) || CastField<FMulticastDelegateProperty>(Property))
        {
            const auto Resolve = [&](const FString& Path) -> UObject*
            {
                for (const auto& Pair : Made)
                    if (P4SameText(File.Node(Pair.Key).Path, Path))
                        return Pair.Value;
                return nullptr;
            };
            if (!GiveDelegate(Property, Live, Place, Field.second, Resolve) && NotHeld)
                NotHeld->AddUnique(Where + TEXT(" = ") + Text(P4MapKit::DecodedJson(Field.second)));
            continue;
        }
        FWriter Writer{File, Made, Final};
        Writer.Lenient = NotHeld != nullptr;
        Writer.MissingAsNotHeld = MissingAsNotHeld && NotHeld != nullptr;
        FDecodedValue EngineTracking;
        const FDecodedValue* SourceValue = &Field.second;
        if (Property->GetFName() == TEXT("UCSModifiedProperties") &&
            Property->GetOwnerStruct() == UActorComponent::StaticClass())
        {
            EngineTracking = Field.second;
            if (P4PrepareOriginalEngineTracking(Class, EngineTracking))
                SourceValue = &EngineTracking;
        }
        const auto PostProcessWrite = P4WriteRecoveredPostProcessSettings(
            Writer, Live, Property, Place, *SourceValue, Where);
        if (PostProcessWrite == EP4RecoveredPostProcessWrite::Refused ||
            (PostProcessWrite == EP4RecoveredPostProcessWrite::NotApplicable &&
                !Writer.Value(Property, Property->ContainerPtrToValuePtr<void>(Live, Place), *SourceValue, Where)))
        {
            Error = Writer.Error;
            return false;
        }
        if (NotHeld)
            for (const FString& Value : Writer.NotHeld)
                NotHeld->AddUnique(Value);
        if (NotShipped)
            for (const FString& Value : Writer.NotShipped)
                NotShipped->AddUnique(Value);
        if (Writer.Later)
            Complete = false;
    }
    if (!Complete && !Final)
    {
        Error = TEXT("level-import-waits-for-an-object-not-made-yet: ") + Node.Path;
        return false;
    }
    return true;
}

// Writes one value as a file says it on an object that is not one of a level being imported: a value
// that names something of the level is refused.
bool PutValue(UObject* Target, const FProperty* Property, int32 Place, const FDecodedValue& Value, const FString& Where,
    FString& Error)
{
    const FFile NoFile;
    const TMap<int32, UObject*> NoneMade;
    FWriter Writer{NoFile, NoneMade, true};
    const auto PostProcessWrite = P4WriteRecoveredPostProcessSettings(Writer, Target, Property, Place, Value, Where);
    if (PostProcessWrite == EP4RecoveredPostProcessWrite::Applied ||
        (PostProcessWrite == EP4RecoveredPostProcessWrite::NotApplicable &&
            Writer.Value(Property, Property->ContainerPtrToValuePtr<void>(Target, Place), Value, Where)))
        return true;
    Error = Writer.Error;
    return false;
}

// Whether an object made from one template has the values of an object made from another. A Blueprint
// class can hold a template of its own for a component it has from a parent class. When that template
// says nothing, it has what the parent's template has, and a project whose class does not hold such an
// empty template makes the component from the parent's. A delegate that calls the template itself is
// bound by the class's own code on every object and is not something the template says. This is read
// from the game's own file of the class. Root is the folder the game's /Game/ packages are under.
// Known keeps what was read: for each template, the one it is made from when it says nothing, else empty.
bool P4SameDecodedValue(const FDecodedValue& A, const FDecodedValue& B)
{
    if (A.Kind != B.Kind || A.Text != B.Text || A.Fields.size() != B.Fields.size() || A.Items.size() != B.Items.size())
        return false;
    for (std::size_t At = 0; At < A.Fields.size(); ++At)
        if (A.Fields[At].first != B.Fields[At].first || !P4SameDecodedValue(A.Fields[At].second, B.Fields[At].second))
            return false;
    for (std::size_t At = 0; At < A.Items.size(); ++At)
        if (!P4SameDecodedValue(A.Items[At], B.Items[At]))
            return false;
    return true;
}

bool ReadTemplate(const FString& Root, const FString& From, FFile& File, const FNode*& Node)
{
    int32 Dot = INDEX_NONE;
    if (!P4IsContentPackageIgnoringCase(From) || !From.FindChar(TEXT('.'), Dot))
        return false;
    FString Package = From.Left(Dot);
    const FString Object = From.Mid(Dot + 1);
    // The compared lines spell object paths in small letters. The engine finds objects by path without
    // regard to case, and the import's record of where it found each class's file is kept under the
    // package's own spelling, which the loaded class's package has.
    if (const UPackage* Loaded = FindPackage(nullptr, *Package))
        Package = Loaded->GetName();
    // The class's file is read where the import found it, as the comparison of the classes reads it: a
    // class need not be in the folder of the level's own file.
    FP4KadanSourceBinding Source;
    FString Error;
    if (!ReadClassSource(Package, Root, Source, Error))
        return false;
    File.Header.assign(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num());
    File.Body.assign(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num());
    if (!ReadNodes(File, FString(), Error) || !File.PackageName.Equals(Package, ESearchCase::IgnoreCase))
        return false;
    Node = File.Nodes.FindByPredicate(
        [&](const FNode& One) { return One.Path.Equals(Object, ESearchCase::IgnoreCase); });
    return Node && Node->Undecoded.IsEmpty() && Node->PlainTail;
}

FString MadeFromWhenEmpty(const FString& Root, const FString& From, TMap<FString, FString>& Known)
{
    if (const FString* Seen = Known.Find(From))
        return *Seen;
    Known.Add(From);
    FFile File;
    const FNode* Node = nullptr;
    if (!ReadTemplate(Root, From, File, Node))
        return FString();
    for (const auto& Field : Node->Values.Fields)
        if (!CallsItself(Field.second, Node->Path))
            return FString();
    const FString Next = File.Ref(Node->Template);
    Known.Add(From, Next);
    return Next;
}

// A class's own template that says what the parent's template says is the same in effect, and the engine drops it
// when the class is compiled again. They are the same when one is made from the other and adds nothing to it, or
// when both are made from one object and say the same. This is read from the game's own files of the classes.
bool SaysTheSame(const FString& Root, const FString& From, const FString& Wanted)
{
    FFile OwnFile, OtherFile;
    const FNode* Own = nullptr;
    const FNode* Other = nullptr;
    if (!ReadTemplate(Root, From, OwnFile, Own) || !ReadTemplate(Root, Wanted, OtherFile, Other) ||
        !Own->Class.Equals(Other->Class, ESearchCase::IgnoreCase))
        return false;
    const FString OwnBase = OwnFile.Ref(Own->Template);
    const bool Over = OwnBase.Equals(Wanted, ESearchCase::IgnoreCase);
    if (!Over && !OwnBase.Equals(OtherFile.Ref(Other->Template), ESearchCase::IgnoreCase))
        return false;
    for (const auto& Field : Own->Values.Fields)
    {
        const FDecodedValue* Same = Other->Values.Field(Field.first);
        if (!CallsItself(Field.second, Own->Path) && (!Same || !P4SameDecodedValue(Field.second, *Same)))
            return false;
    }
    if (!Over)
        for (const auto& Field : Other->Values.Fields)
            if (!CallsItself(Field.second, Other->Path) && !Own->Values.Field(Field.first))
                return false;
    return true;
}

// Two templates that each say nothing over a third have its values. A class's own template of a parent's
// component is saved over the template of the same name in a parent class, and over the component class's
// default object when its name is another (the editor names it ICH-<variable> when the class has a
// component of that name of its own): the two are the same in effect when both lead, through templates
// that say nothing, to one object.
bool SameInEffect(const FString& Root, FString From, const FString& Wanted, TMap<FString, FString>& Known)
{
    const FString Asked = From;
    TArray<FString> Through;
    for (int32 Guard = 0; Guard < 8 && !From.IsEmpty(); ++Guard)
    {
        Through.Add(From);
        From = MadeFromWhenEmpty(Root, From, Known);
    }
    FString Other = Wanted;
    for (int32 Guard = 0; Guard < 8 && !Other.IsEmpty(); ++Guard)
    {
        if (Through.ContainsByPredicate([&](const FString& One) { return One.Equals(Other, ESearchCase::IgnoreCase); }))
            return true;
        Other = MadeFromWhenEmpty(Root, Other, Known);
    }
    return SaysTheSame(Root, Asked, Wanted);
}

#if P4_KADAN_TEST_SEAMS
// Optional typed-null diagnostics.
void TraceNullOverrides(
    const FFile& Compared, const FFile& Mine, const FNode& Node, const FNode* Cooked, const UObject* LiveOwner)
{
    if (Node.Values.Field("OverrideMaterials") &&
        FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CLASS_COMPARE_TRACE")) == TEXT("1"))
    {
        const UObject* Live = LiveOwner
            ? StaticFindObject(UObject::StaticClass(), LiveOwner->GetOutermost(), *Node.Path)
            : nullptr;
        UE_LOG(LogTemp, Display,
            TEXT(
                "Null overrides comparison %s | live %s | source template %s | cooked template %s | source %s | cooked %s"),
            *Node.Path, *GetPathNameSafe(Live), *Compared.Ref(Node.Template),
            Cooked ? *Mine.Ref(Cooked->Template) : TEXT("missing"), *Text(P4MapKit::DecodedJson(Node.Values)),
            Cooked ? *Text(P4MapKit::DecodedJson(Cooked->Values)) : TEXT("missing"));
    }
}
#endif

// Compares source and cooked null material defaults.
// A source template can name a package absent from the game's containers. That
// reference resolves to null. A level's explicit null override then has the same
// value as its template, so this engine omits the redundant tag when it cooks.
// Establish that from both files; other missing tags remain comparison failures.
void RedundantUnshippedNulls(
    FFile& Compared, const FFile& Mine, const FString& Root, TArray<FString>& NotShipped, UObject* LiveOwner)
{
    TMap<FString, TSharedPtr<FFile>> Templates;
    for (FNode& Node : Compared.Nodes)
    {
        const FNode* Cooked = Mine.Nodes.FindByPredicate(
            [&](const FNode& Other) { return P4SameText(Node.Path, Other.Path); });
#if P4_KADAN_TEST_SEAMS
        TraceNullOverrides(Compared, Mine, Node, Cooked, LiveOwner);
#endif
        if (!Cooked || !P4SameText(Node.Class, Cooked->Class) ||
            !P4SameText(Compared.Ref(Node.Template), Mine.Ref(Cooked->Template)))
            continue;
        if (LiveOwner)
            P4WithoutRedundantNullMaterialOverrides(
                StaticFindObject(UObject::StaticClass(), LiveOwner->GetOutermost(), *Node.Path), Node.Values,
                Cooked->Values);
        const FString From = Compared.Ref(Node.Template);
        int32 Dot = INDEX_NONE;
        if (Root.IsEmpty() || !P4IsContentPackage(From) || !From.FindChar(TEXT('.'), Dot))
            continue;
        const FString Package = From.Left(Dot), Object = From.Mid(Dot + 1);
        TSharedPtr<FFile>* Seen = Templates.Find(Package);
        if (!Seen)
        {
            TSharedPtr<FFile> File = MakeShared<FFile>();
            TArray<uint8> Head, Body;
            FString Error;
            const FString Header = Root / P4ContentRelativePath(Package) + TEXT(".uasset");
            if (!FFileHelper::LoadFileToArray(Head, *Header, FILEREAD_Silent) ||
                !FFileHelper::LoadFileToArray(Body, *FPaths::ChangeExtension(Header, TEXT("uexp")), FILEREAD_Silent))
                File.Reset();
            if (File.IsValid())
            {
                File->Header.assign(Head.GetData(), Head.GetData() + Head.Num());
                File->Body.assign(Body.GetData(), Body.GetData() + Body.Num());
                if (!ReadNodes(*File, FString(), Error) || !P4SameText(File->PackageName, Package))
                    File.Reset();
            }
            Seen = &Templates.Add(Package, File);
        }
        if (!Seen->IsValid())
            continue;
        const FNode* Template = (*Seen)->Nodes.FindByPredicate(
            [&](const FNode& One) { return P4SameText(One.Path, Object); });
        if (!Template || !Template->Undecoded.IsEmpty())
            continue;
        for (std::size_t Index = Node.Values.Fields.size(); Index-- > 0;)
        {
            const auto& Field = Node.Values.Fields[Index];
            if (Field.second.Kind != FDecodedValue::EKind::Null || Cooked->Values.Field(Field.first))
                continue;
            const FDecodedValue* Original = Template->Values.Field(Field.first);
            if (!Original || Original->Kind != FDecodedValue::EKind::String || Original->Text != Unshipped)
                continue;
            NotShipped.AddUnique(Node.Path + TEXT(" ") + Text(Field.first) + TEXT(" = ") + Text(Unshipped));
            Node.Values.Fields.erase(Node.Values.Fields.begin() + Index);
        }
    }
}

// Takes out of what a file says the values told as not held, so that everything else it says is
// compared. A value is told as "Path Name.Member[2].Sub = value": Name is a property of the object at
// Path, and what follows leads to the member. One that is not found where it is told stays, and shows
// as a difference.
void Without(FFile& File, const TArray<FString>& NotHeld)
{
    for (const FString& Told : NotHeld)
    {
        int32 Space = INDEX_NONE;
        const int32 Equals = Told.Find(TEXT(" = "), ESearchCase::CaseSensitive);
        if (!Told.FindChar(TEXT(' '), Space) || Equals <= Space)
            continue;
        const FString Path = Told.Left(Space);
        TArray<FString> Steps;
        Told.Mid(Space + 1, Equals - Space - 1).ParseIntoArray(Steps, TEXT("."), false);
        FNode* Node = File.Nodes.FindByPredicate([&](const FNode& One) { return P4SameText(One.Path, Path); });
        FDecodedValue* At = Node ? &Node->Values : nullptr;
        for (int32 Step = 0; At && Step < Steps.Num(); ++Step)
        {
            const auto Find = [&](const FString& Key)
            {
                for (int32 Slot = 0; At->Kind == FDecodedValue::EKind::Object && Slot < int32(At->Fields.size());
                    ++Slot)
                    if (P4SameText(Text(At->Fields[Slot].first), Key))
                        return Slot;
                return int32(INDEX_NONE);
            };
            // A step is a member's name, with the places in a list after it. A property that is a fixed
            // list says its later places in its name.
            FString Name = Steps[Step];
            TArray<int32> Places;
            int32 Bracket = INDEX_NONE;
            int32 Found = Find(Name);
            while (Found == INDEX_NONE && Name.EndsWith(TEXT("]"), ESearchCase::CaseSensitive) &&
                Name.FindLastChar(TEXT('['), Bracket))
            {
                Places.Insert(FCString::Atoi(*Name.Mid(Bracket + 1)), 0);
                Name.LeftInline(Bracket);
                Found = Find(Name);
            }
            if (Found == INDEX_NONE)
                break;
            if (Step == Steps.Num() - 1 && !Places.Num())
            {
                At->Fields.erase(At->Fields.begin() + Found);
                break;
            }
            At = &At->Fields[Found].second;
            for (const int32 Place : Places)
            {
                if (At->Kind != FDecodedValue::EKind::Array || Place < 0 || Place >= int32(At->Items.size()))
                {
                    At = nullptr;
                    break;
                }
                At = &At->Items[Place];
            }
        }
    }
}
} // namespace P4LevelContent
