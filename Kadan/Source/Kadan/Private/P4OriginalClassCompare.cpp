// Comparing a Blueprint class of the project with the game's file of the class.

#include "P4LevelContentInternal.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4ImportIdentityMemory.h"

namespace P4LevelContent
{
// A level's file says of an object only what differs from what its class gives it. A class of the
// project that does not say what the game's file of the class says gives something else, so a level
// made from the game's file with it is not the game's level wherever the file is silent, and its
// objects cannot be compared with the file line by line. For the making and the comparing, such a
// class's own objects (its default object and the templates of its components) are given what the
// game's file of the class says, and afterwards they get back what they had: the class in the project
// is not changed. The level's objects keep the values, which the level then says for itself, so a
// saved level gives them back whatever the project's class says. What cannot be made right this way
// is an actor of such a class in a mod without more: a mod's level says only what differs from the
// project's class, and the game would fill the rest from its own. So the export lends the same way
// while it reads the level and cooks the mod's level, when it is given the game's files of the classes:
// the copy then says what differs from the game's class, which is what the game needs. Without the
// files such an actor is refused.
// Class input comes from this import run's exact acquired registry. Standalone explicit
// level-source callers may use only the matching /Game tree supplied with that level.
// A registered missing/ambiguous input never falls through to another cache.
bool ReadClassSource(const FString& Package, const FString& Root, FP4KadanSourceBinding& Source, FString& Error)
{
    if (P4KadanHasBoundSource(Package))
        return P4KadanReadBoundSource(Package, Source, Error);
    if (Root.IsEmpty())
    {
        Error = TEXT("the game's class source is not registered and no explicit source tree was supplied");
        return false;
    }
    auto Row = MakeShared<FP4KadanSourceAsset>();
    Row->Package = Package;
    Row->Header = Root / P4ContentRelativePath(Package) + TEXT(".uasset");
    Row->Body = FPaths::ChangeExtension(Row->Header, TEXT("uexp"));
    FP4KadanImportSourceScope ExplicitSource({Row});
    return P4KadanReadBoundSource(Package, Source, Error);
}

// The Blueprint classes a level's objects are of, and the classes those come from, each asked whether
// it says what the game's file of the class says. Root is the folder the game's /Game/ packages are
// under. Gives one line for each class that differs or could not be asked, and how many were asked.
TArray<FString> ClassesThatDiffer(const FFile& File, const FString& Root, int32& Asked, TArray<FString>* Differing,
    TSet<FName>* Unproven, TMap<FString, FP4KadanSourceBinding>* Sources)
{
    TArray<FString> Packages, Out;
    for (const FNode& Node : File.Nodes)
    {
        int32 Dot = INDEX_NONE;
        if (!P4IsContentPackage(Node.Class) || !Node.Class.FindChar(TEXT('.'), Dot))
            continue;
        // A missing generated class still requires source proof; it cannot vanish from the
        // proof inventory merely because LoadObject failed.
        const FString DeclaredPackage = FPackageName::ObjectPathToPackageName(Node.Class);
        // The level's own script is decoded and qualified with the level (including its parent).
        // Loading it here reloads an earlier saved world while reimport is building its replacement.
        if (P4SameText(DeclaredPackage, File.PackageName))
            continue;
        Packages.AddUnique(DeclaredPackage);
        // The class, and every Blueprint class it comes from.
        for (const UClass* Class = LoadObject<UClass>(nullptr, *Node.Class, nullptr, LOAD_NoWarn | LOAD_Quiet); Class;
            Class = Class->GetSuperClass())
        {
            const FString Package = Class->GetOutermost()->GetName();
            if (!P4IsContentPackage(Package))
                break;
            // The level's own script class is in the level's file and is compared with it.
            if (!P4SameText(Package, File.PackageName))
                Packages.AddUnique(Package);
        }
    }
    Packages.Sort();
    Asked = 0;
    for (const FString& Package : Packages)
    {
        FP4KadanSourceBinding Source;
        FString Error;
        if (!ReadClassSource(Package, Root, Source, Error))
        {
            Out.Add(Package + TEXT(": ") + Error);
            if (Unproven)
                Unproven->Add(FName(*Package));
            continue;
        }
        const FP4OriginalClassComparisonResult Said = CompareClassNative(
            Source.HeaderPath, Source.BodyPath, Package, &Source.Header, &Source.Body);
        ++Asked;
        if (!Said.IsValid() || !Said.DefaultsReadable || !P4KadanSourceBindingCurrent(Source, Error))
        {
            Out.Add(Package + TEXT(": ") +
                (!Error.IsEmpty()                    ? Error
                        : Said.ErrorDetail.IsEmpty() ? TEXT("the game's class defaults were not fully readable")
                                                     : Said.ErrorDetail));
            for (int32 Node = 0; Node < FMath::Min(4, Said.UnreadableDefaultNodes.Num()); ++Node)
                Out.Add(Package + TEXT(": ") + Said.UnreadableDefaultNodes[Node]);
            if (Unproven)
                Unproven->Add(FName(*Package));
            continue;
        }
        if (Sources)
            Sources->Add(Package, Source);
        if (Said.Same)
            continue;
        if (Differing)
            Differing->Add(Package);
        FString First;
        if (Said.OnlyGameSays.Num())
            First = TEXT("only the game says ") + Said.OnlyGameSays[0].Left(220);
        else if (Said.OnlyProjectSays.Num())
            First = TEXT("only the project says ") + Said.OnlyProjectSays[0].Left(220);
        Out.Add(Package + FString::Printf(TEXT(": %d lines differ, "), Said.Differences) + First);
    }
    return Out;
}

// Takes out of a value what a type of this engine has no property for, and counts it.
void Unheld(const UStruct* Type, FDecodedValue& Value, int32& Count, int32 Depth,
    TArray<FP4OriginalClassValueNotHeld>* Details, const FString& Object, const FString& Prefix,
    const FP4RecoveredPostProcessStorage* LogicalSettings, FString* StorageError, const UObject* StockOwner)
{
    if (!Type || Depth > 16)
        return;
    if (Value.Kind != FDecodedValue::EKind::Object)
    {
        if (LogicalSettings && StorageError && StorageError->IsEmpty())
            *StorageError = TEXT("level-import-postprocess-value-is-not-a-struct");
        return;
    }
    FP4RecoveredPostProcessStorage Storage;
    bool HasStorage = false;
    if (const UClass* Class = Cast<UClass>(Type))
    {
        FString Error;
        HasStorage = P4GetRecoveredPostProcessStorage(Class, Storage, Error);
        if (!HasStorage && !Error.IsEmpty())
        {
            if (StorageError && StorageError->IsEmpty())
                *StorageError = Error;
            return;
        }
    }
    // Match the exact adapter's syntax and duplicate contract before removing
    // unsupported members. A malformed source cannot become a Count==0 proof.
    if (LogicalSettings)
    {
        TSet<FString> Seen;
        for (const auto& Field : Value.Fields)
        {
            const FString Original = Text(Field.first);
            FString Name;
            int32 Place;
            if (!P4RecoveredPostProcessOriginalMemberIndex(Original, Name, Place))
            {
                if (StorageError && StorageError->IsEmpty())
                    *StorageError = TEXT("postprocess-invalid-fixed-member: ") + Original;
                return;
            }
            const FString Identity = Name.ToLower() + FString::Printf(TEXT("[%d]"), Place);
            if (Seen.Contains(Identity))
            {
                if (StorageError && StorageError->IsEmpty())
                    *StorageError = TEXT("postprocess-duplicate-original-member: ") + Original;
                return;
            }
            Seen.Add(Identity);
        }
    }
    for (std::size_t Slot = Value.Fields.size(); Slot-- > 0;)
    {
        const FString OriginalKey = Text(Value.Fields[Slot].first);
        const FString FullPath = Prefix.IsEmpty() ? OriginalKey : Prefix + TEXT(".") + OriginalKey;
        FString Key = OriginalKey;
        int32 Bracket = INDEX_NONE, Place = 0;
        if (LogicalSettings)
            P4RecoveredPostProcessOriginalMemberIndex(OriginalKey, Key, Place);
        else if (Key.FindChar(TEXT('['), Bracket))
            Key.LeftInline(Bracket);
        FP4RecoveredPostProcessMember LogicalMember;
        const FProperty* Property = LogicalSettings
            ? (P4ResolveRecoveredPostProcessMember(*LogicalSettings, FName(*Key), LogicalMember)
                      ? LogicalMember.Property
                      : nullptr)
            : Type->FindPropertyByName(FName(*Key));
        if (HasStorage && Property == Storage.ForkSettings)
        {
            P4AppendOriginalClassUnheldDetail(Details, Object, FullPath, Type->GetPathName(), Value.Fields[Slot].second,
                TEXT("host-storage-is-not-an-original-property"));
            Value.Fields.erase(Value.Fields.begin() + Slot);
            ++Count;
            continue;
        }
        if (!Property && StockOwner && Depth == 0 && Prefix.IsEmpty() && Bracket == INDEX_NONE &&
            P4CanStoreStockEngineField(StockOwner, FName(*Key)))
        {
            bool Held = false;
            for (const auto& Field : P4GetStockEngineFields(StockOwner))
                if (Field.Name == FName(*Key) &&
                    P4MapKit::DecodedJson(P4DecodeStockEngineField(Field)) ==
                        P4MapKit::DecodedJson(Value.Fields[Slot].second))
                    Held = true;
            if (Held)
                continue; // The actual record and the actual emitted cook are both compared.
        }
        if (!Property || (LogicalSettings && (Place < 0 || Place >= Property->ArrayDim)))
        {
            P4AppendOriginalClassUnheldDetail(Details, Object, FullPath, Type->GetPathName(), Value.Fields[Slot].second,
                Property ? TEXT("fixed-array-index-not-in-native-schema") : TEXT("property-not-in-native-schema"));
            Value.Fields.erase(Value.Fields.begin() + Slot);
            ++Count;
            continue;
        }
        FDecodedValue& Said = Value.Fields[Slot].second;
        // The native writer explicitly records a named enum choice absent from this engine.
        // Class comparison must apply that same rule, rather than expect the skipped choice
        // to appear in the editor's cook. Known choices and arbitrary numeric data stay compared.
        const FEnumProperty* Enum = CastField<FEnumProperty>(Property);
        const FByteProperty* Byte = CastField<FByteProperty>(Property);
        const UEnum* Choices = Enum ? Enum->GetEnum() : Byte ? Byte->Enum : nullptr;
        FString EnumName = Text(Said.Text);
        const bool EnumMapped = !LogicalMember.OriginalDepthOfFieldMethod ||
            Said.Kind != FDecodedValue::EKind::String ||
            P4RecoveredPostProcessSourceEnum(LogicalMember, Text(Said.Text), EnumName);
        if (Choices && Said.Kind == FDecodedValue::EKind::String &&
            (!EnumMapped || Choices->GetValueByNameString(EnumName) == INDEX_NONE))
        {
            P4AppendOriginalClassUnheldDetail(
                Details, Object, FullPath, Type->GetPathName(), Said, TEXT("enum-choice-not-in-native-schema"));
            Value.Fields.erase(Value.Fields.begin() + Slot);
            ++Count;
            continue;
        }
        if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            // Which navigation agents something is for is one word of bits, saved by the engine as it is
            // and written so by the import (P4DecodedPropertyWriter): it is held, though no property of the
            // struct is named for it.
            if (Struct->Struct &&
                Struct->Struct->GetPathName().EndsWith(TEXT(".NavAgentSelector"), ESearchCase::CaseSensitive) &&
                Struct->Struct->GetStructureSize() == 4 && Said.Kind == FDecodedValue::EKind::Object &&
                Said.Fields.size() == 1 && Said.Fields[0].first == "PackedBits" &&
                Said.Fields[0].second.Kind == FDecodedValue::EKind::Number)
                continue;
            Unheld(Struct->Struct, Said, Count, Depth + 1, Details, Object, FullPath,
                HasStorage && Property == Storage.StockSettings ? &Storage : nullptr, StorageError);
        }
        else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            const FStructProperty* Inner = CastField<FStructProperty>(Array->Inner);
            if (Inner && Said.Kind == FDecodedValue::EKind::Array)
                for (std::size_t Index = 0; Index < Said.Items.size(); ++Index)
                    Unheld(Inner->Struct, Said.Items[Index], Count, Depth + 1, Details, Object,
                        FullPath + FString::Printf(TEXT("[%d]"), static_cast<int32>(Index)), nullptr, StorageError);
        }
    }
}

// Whether a Blueprint class of the project says what the game's file of the class says: its default
// object, the templates of its components and the nodes of its construction script, each with what it
// is made from. Not its code: the class and its functions are left out. A level says only what differs
// from these, so a level is what the game's file says only when they are the same. Values of
// properties this engine does not have are taken out of what the game's file says and counted.
// Empty transient compiler frames contain no authored defaults. Require their original native
// layout, CDO/function linkage and exact empty tagged payload; every other field stays compared.
int32 ClassifyEmptyCompilerFrames(FFile& File)
{
    int32 Count = 0;
    for (int32 I = 0; I < File.Nodes.Num(); ++I)
    {
        FNode& Node = File.Nodes[I];
        const FDecodedValue* Frame = Node.Values.Field("UberGraphFrame");
        if (!Frame || Frame->Kind != FDecodedValue::EKind::Object || !Frame->Fields.empty() || !Frame->Items.empty() ||
            !P4MapKit::Blueprint::IsProvenEmptyCompilerFrame(File.Package, File.Body, I + 1))
            continue;
        Node.Values.Fields.erase(
            std::remove_if(Node.Values.Fields.begin(), Node.Values.Fields.end(),
                [](const std::pair<std::string, FDecodedValue>& Field) { return Field.first == "UberGraphFrame"; }),
            Node.Values.Fields.end());
        ++Count;
    }
    return Count;
}

#if P4_KADAN_TEST_SEAMS
// Optional source and project node diagnostics.
void TraceClassComponentNodes(const FFile& File, const TCHAR* Side)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CLASS_COMPARE_TRACE")) != TEXT("1"))
        return;
    for (const FNode& Node : File.Nodes)
        if (P4SameText(Node.Class, TEXT("/Script/Engine.SCS_Node")))
            UE_LOG(LogTemp, Display, TEXT("Class component node %s %s %s"), Side, *Node.Path,
                *Text(P4MapKit::DecodedJson(Node.Values)));
}

// Optional canonical parent-key diagnostics.
void TraceClassParentKeys(const TArray<FString>& GameKeys, const TArray<FString>& MineKeys)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CLASS_COMPARE_TRACE")) == TEXT("1"))
    {
        for (const FString& Line : GameKeys)
            if (Line.Contains(TEXT("parentcomponentorvariablename")))
                UE_LOG(LogTemp, Display, TEXT("Class compared source key %s"), *Line);
        for (const FString& Line : MineKeys)
            if (Line.Contains(TEXT("parentcomponentorvariablename")))
                UE_LOG(LogTemp, Display, TEXT("Class compared project key %s"), *Line);
    }
}
#endif

// The canonical keys only one side says, at most 60 of each, and how many there are. A source key the
// class's constructor attaches is not one. When some source keys are not held, the source's default
// facts digest is made again from the held ones.
int32 ClassDefaultDifferences(FP4OriginalClassComparisonResult& Result, const TArray<FString>& GameKeys,
    const TArray<FString>& MineKeys, const TArray<FString>& MineSays, const TSet<FString, FLocKeySetFuncs>& InGame,
    const TSet<FString, FLocKeySetFuncs>& InMine, TFunctionRef<bool(const FString&)> AttachedByConstructor,
    TFunctionRef<bool(const TArray<FString>&, FString&)> FactsDigest, TArray<FString>& OnlyGame,
    TArray<FString>& OnlyMine)
{
    int32 Differences = 0;
    TArray<FString> GameHeld;
    for (int32 At = 0; At < GameKeys.Num(); ++At)
    {
        if (InMine.Contains(GameKeys[At]))
        {
            GameHeld.Add(GameKeys[At]);
            continue;
        }
        if (AttachedByConstructor(GameKeys[At]))
            continue;
        GameHeld.Add(GameKeys[At]);
        if (OnlyGame.Num() < 60)
            OnlyGame.Add(GameKeys[At]);
        ++Differences;
    }
    if (GameHeld.Num() != GameKeys.Num() && !FactsDigest(GameHeld, Result.GameDefaultFactsDigest))
    {
        Result.GameDefaultFactsDigest.Reset();
        Result.DefaultFactsDigest.Reset();
        Result.DefaultsReadable = false;
    }
    for (int32 At = 0; At < MineKeys.Num(); ++At)
        if (!InGame.Contains(MineKeys[At]))
        {
#if P4_KADAN_TEST_SEAMS
            if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CLASS_COMPARE_TRACE")) == TEXT("1"))
                UE_LOG(LogTemp, Display, TEXT("Class unmatched project key %s | displayed as %s"), *MineKeys[At],
                    *MineSays[At]);
#endif
            if (OnlyMine.Num() < 60)
                OnlyMine.Add(MineKeys[At]);
            ++Differences;
        }
    return Differences;
}

FP4OriginalClassComparisonResult CompareClassNative(const FString& HeaderFile, const FString& BodyFile,
    const FString& OwnerPackage, const TArray<uint8>* SourceHeader, const TArray<uint8>* SourceBody)
{
    FP4OriginalClassComparisonResult Result;
    const auto Refuse = [&](EP4OriginalClassComparisonError Code, const FString& Detail)
    {
        Result.Error = Code;
        Result.ErrorDetail = Detail;
        return Result;
    };
    if (!IsInGameThread())
        return Refuse(
            EP4OriginalClassComparisonError::EditorThreadRequired, TEXT("class-compare-requires-editor-thread"));
    FFile Game;
    FString Error;
    if (SourceHeader || SourceBody)
    {
        if (!SourceHeader || !SourceBody || SourceHeader->Num() == 0 || SourceBody->Num() == 0)
            return Refuse(EP4OriginalClassComparisonError::SourceUnreadable, TEXT("class-compare-source-unreadable"));
        Game.Header.assign(SourceHeader->GetData(), SourceHeader->GetData() + SourceHeader->Num());
        Game.Body.assign(SourceBody->GetData(), SourceBody->GetData() + SourceBody->Num());
    }
    else
    {
        TArray<uint8> Header, Body;
        if (!FFileHelper::LoadFileToArray(Header, *HeaderFile, FILEREAD_Silent) ||
            !FFileHelper::LoadFileToArray(Body,
                *(BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile), FILEREAD_Silent))
            return Refuse(EP4OriginalClassComparisonError::SourceUnreadable, TEXT("class-compare-source-unreadable"));
        Game.Header.assign(Header.GetData(), Header.GetData() + Header.Num());
        Game.Body.assign(Body.GetData(), Body.GetData() + Body.Num());
    }
    // An import retoc could not name is read with the identity the Blueprint planner verified for these bytes:
    // a mesh the game ships in a package retoc does not know is not a reference to nothing.
    // Only an object the project holds is named: a game asset the import keeps as the game's (a sound, an effect)
    // stays a reference to nothing on both sides, as the Blueprint's value for it is.
    {
        const auto Named = P4MapKit::ReadPackage(Game.Header, Game.Body);
        if (Named.Error.Code.empty())
            for (const auto& Identity : P4RememberedImportIdentities(
                     OwnerPackage.IsEmpty() ? Text(Named.Package) : OwnerPackage, Game.Header, Game.Body))
            {
                const FString Path = Text(Identity.second);
                const FString Package = FPackageName::ObjectPathToPackageName(Path);
                if (FindObject<UObject>(nullptr, *Path) || FPackageName::DoesPackageExist(Package))
                    Game.RecoveredImports.emplace(Identity.first, Identity.second);
            }
    }
    if (!ReadNodes(Game, OwnerPackage, Error))
        return Refuse(EP4OriginalClassComparisonError::DecodeFailed, Error);
#if P4_KADAN_TEST_SEAMS
    TraceClassComponentNodes(Game, TEXT("source"));
#endif
    const FString Package = Game.PackageName, Short = FPackageName::GetLongPackageAssetName(Package);
    Result.Package = Package;
    Result.PackageKnown = true;
    if (!FPackageName::DoesPackageExist(Package))
        return Refuse(EP4OriginalClassComparisonError::ClassUnavailable,
            TEXT("class-compare-class-not-in-the-project: ") + Package);
    UPackage* Loaded = LoadPackage(nullptr, *Package, LOAD_NoWarn | LOAD_Quiet);
    UBlueprint* Blueprint = Loaded ? FindObject<UBlueprint>(Loaded, *Short) : nullptr;
    if (!Blueprint || !Blueprint->GeneratedClass)
        return Refuse(EP4OriginalClassComparisonError::BlueprintUnavailable,
            TEXT("class-compare-no-blueprint-at-the-path: ") + Package);
    FFile Mine;
    if (!Cooked(Blueprint, false, Mine, Error, FString(), &Game))
        return Refuse(EP4OriginalClassComparisonError::CookFailed, Error);
#if P4_KADAN_TEST_SEAMS
    TraceClassComponentNodes(Mine, TEXT("project"));
#endif
    for (FNode& Node : Game.Nodes)
        if (const FNode* CookedNode = Mine.Nodes.FindByPredicate(
                [&](const FNode& Other) { return P4SameText(Node.Path, Other.Path); }))
            P4WithoutRedundantNullMaterialOverrides(
                ResolveOriginalExternalTemplate(Package + TEXT(".") + Node.Path), Node.Values, CookedNode->Values);
    Result.CompilerFramesRebuilt = ClassifyEmptyCompilerFrames(Game);
    Result.EditorCompilerFramesRebuilt = ClassifyEmptyCompilerFrames(Mine);
    // What this engine has no property for is not something the project's class can say.
    int32 NotHeld = 0;
    for (FNode& Node : Game.Nodes)
    {
        const UObject* Live = StaticFindObject(UObject::StaticClass(), Loaded, *Node.Path);
        // Cook-only component exports need not exist as objects in the editor package.
        // Their declared registered class still supplies the actual property schema.
        // Unknown classes stay fully compared; never infer a schema from field names.
        const UClass* Schema = Live ? Live->GetClass() : FindObject<UClass>(nullptr, *Node.Class);
        if (Schema)
        {
            FString StorageError;
            Unheld(Schema, Node.Values, NotHeld, 0, &Result.ValuesNotHeldDetails, Node.Path, FString(), nullptr,
                &StorageError, Live);
            if (!StorageError.IsEmpty())
                return Refuse(EP4OriginalClassComparisonError::DecodeFailed, StorageError);
        }
    }
    Result.ValuesNotHeld = NotHeld;
    Result.ValuesKnown = true;
    // A reference from one of a class's own objects to another (CameraRoot attached to Scene). A cook leaves it
    // out when the parent's default object has the same reference to its own object of that name, as a stub's
    // constructor makes it. A class just built can still write it, and the same class loaded from its file does
    // not. The object in the project is read: when it holds what the game's file says, the class says the same.
    for (FNode& Node : Game.Nodes)
    {
        const FNode* Made =
            Mine.Nodes.FindByPredicate([&](const FNode& Other) { return P4SameText(Node.Path, Other.Path); });
        const UObject* Live = Made ? StaticFindObject(UObject::StaticClass(), Loaded, *Node.Path) : nullptr;
        if (!Live)
            continue;
        auto& Fields = Node.Values.Fields;
        for (std::size_t Slot = Fields.size(); Slot-- > 0;)
        {
            if (Made->Values.Field(Fields[Slot].first))
                continue;
            const FObjectPropertyBase* Property =
                CastField<FObjectPropertyBase>(Live->GetClass()->FindPropertyByName(FName(*Text(Fields[Slot].first))));
            const UObject* Holds = Property ? Property->GetObjectPropertyValue_InContainer(Live) : nullptr;
            if (Holds && Holds->GetOutermost() == Loaded &&
                P4SameText(Holds->GetPathName(Loaded).Replace(SUBOBJECT_DELIMITER, TEXT(".")),
                    Text(Fields[Slot].second.Text)))
                Fields.erase(Fields.begin() + Slot);
        }
    }
    // The class itself and its functions hold code, which is compared elsewhere.
    const auto Code = [](const FFile& File)
    {
        TSet<FString, FLocKeySetFuncs> Paths;
        for (const FNode& Node : File.Nodes)
            if (Node.Class.EndsWith(TEXT("GeneratedClass"), ESearchCase::CaseSensitive) ||
                P4SameText(Node.Class, TEXT("/Script/CoreUObject.Function")))
                Paths.Add(Node.Path);
        return Paths;
    };
    // A class can hold a template of its own for a component it has from a parent class. One that says
    // nothing (a delegate that calls the template itself does not count) has what the parent's template
    // has, and a class without it makes the same component. Such templates are left out of what the
    // game's file says, with the class's record of them, and counted.
    TSet<FString, FLocKeySetFuncs> GameLeft = Code(Game), MineLeft = Code(Mine);
    Result.DefaultsReadable = true;
    for (const FFile* File : {&Game, &Mine})
    {
        const TSet<FString, FLocKeySetFuncs>& Left = File == &Game ? GameLeft : MineLeft;
        for (const FNode& Node : File->Nodes)
            if (!Left.Contains(Node.Path) &&
                (!Node.Undecoded.IsEmpty() || (!Node.PlainTail && Node.Lods < 0 && Node.EngineTail.IsEmpty())))
            {
                Result.DefaultsReadable = false;
                if (Result.UnreadableDefaultNodes.Num() < 64)
                {
                    FString Bytes;
                    for (std::size_t Byte = Node.TailStart; Byte < Node.TailEnd && Byte < Node.TailStart + 24; ++Byte)
                        Bytes += FString::Printf(TEXT("%02x"), File->Body[Byte]);
                    Result.UnreadableDefaultNodes.Add(FString::Printf(
                        TEXT(
                            "%s %s | class %s | flags %08x | tail %d bytes | properties %s | native skinned=%d physics=%d body=%d | bytes %s"),
                        File == &Game ? TEXT("source") : TEXT("editor"), *Node.Path, *Node.Class, Node.Flags,
                        int32(Node.TailEnd - Node.TailStart),
                        Node.Undecoded.IsEmpty() ? TEXT("decoded") : *Node.Undecoded, Node.Skinned, Node.Physics,
                        Node.PhysicsBody, *Bytes));
                }
            }
    }
    int32 EmptyTemplates = 0, RecordIds = 0;
    // A class's record of such templates names each by its component's variable, with the id the
    // editor gave the variable, in the order they were made. The ids are left out, the records are told
    // in one order, and a record that names none is no record.
    const auto Records = [&](FFile& File, TSet<FString, FLocKeySetFuncs>& Left)
    {
        for (FNode& Node : File.Nodes)
        {
            if (!P4SameText(Node.Class, TEXT("/Script/Engine.InheritableComponentHandler")))
                continue;
            bool Any = false;
            for (auto& Field : Node.Values.Fields)
            {
                if (Field.first != "Records" || Field.second.Kind != FDecodedValue::EKind::Array)
                    continue;
                auto& Items = Field.second.Items;
                for (FDecodedValue& Item : Items)
                    for (auto& Member : Item.Fields)
                    {
                        if (Member.first != "ComponentKey")
                            continue;
                        auto& Parts = Member.second.Fields;
                        for (std::size_t Slot = Parts.size(); Slot-- > 0;)
                            if (Parts[Slot].first == "AssociatedGuid")
                            {
                                Parts.erase(Parts.begin() + Slot);
                                ++RecordIds;
                            }
                    }
                std::stable_sort(Items.begin(), Items.end(),
                    [](const FDecodedValue& A, const FDecodedValue& B)
                    {
                        const FDecodedValue* One = A.Field("ComponentTemplate");
                        const FDecodedValue* Other = B.Field("ComponentTemplate");
                        return (One ? One->Text : std::string()) < (Other ? Other->Text : std::string());
                    });
                Any = Any || !Items.empty();
            }
            if (!Any)
                Left.Add(Node.Path);
        }
    };
    {
        // The parent's template each of the class's own templates is for, from the class's record: the
        // template of the variable in the class that has the component.
        TMap<FString, FString> Overrides;
        for (const FNode& Node : Game.Nodes)
        {
            if (!P4SameText(Node.Class, TEXT("/Script/Engine.InheritableComponentHandler")))
                continue;
            const FDecodedValue* List = Node.Values.Field("Records");
            for (std::size_t Slot = 0; List && List->Kind == FDecodedValue::EKind::Array && Slot < List->Items.size();
                ++Slot)
            {
                const FDecodedValue& Item = List->Items[Slot];
                const FDecodedValue* Template = Item.Field("ComponentTemplate");
                const FDecodedValue* Key = Item.Field("ComponentKey");
                const FDecodedValue* Owner = Key ? Key->Field("OwnerClass") : nullptr;
                const FDecodedValue* Variable = Key ? Key->Field("SCSVariableName") : nullptr;
                if (Template && Owner && Variable && Template->Kind == FDecodedValue::EKind::String &&
                    Owner->Kind == FDecodedValue::EKind::String && Variable->Kind == FDecodedValue::EKind::String &&
                    !Owner->Text.empty() && !Variable->Text.empty())
                    Overrides.Add(Text(Template->Text),
                        Text(Owner->Text) + TEXT(".") + Text(Variable->Text) + TEXT("_GEN_VARIABLE"));
            }
        }
        // The folder the game's /Game/ packages are under, from where the class's own file is, for the
        // files of its parents that the import has not recorded.
        FString Root = HeaderFile;
        FPaths::NormalizeFilename(Root);
        {
            const FString Tail = P4ContentRelativePath(Package) + TEXT(".uasset");
            Root = Root.EndsWith(Tail, ESearchCase::IgnoreCase) ? Root.LeftChop(Tail.Len()) : FString();
        }
        TMap<FString, FString> Known;
        TSet<FString, FLocKeySetFuncs> Empty;
        // Case-insensitive, as the engine finds objects: the project's file may spell a path another way.
        TSet<FString> SaysTheParents;
        for (const FNode& Node : Game.Nodes)
        {
            const bool Template = Node.Undecoded.IsEmpty() && Node.PlainTail &&
                (Node.Flags & uint32(RF_InheritableComponentTemplate));
            bool Says = !Template;
            for (const auto& Field : Node.Values.Fields)
                Says = Says || !CallsItself(Field.second, Node.Path);
            // A template with values of its own that are the parent's template's values is the same in effect.
            // The engine drops it when it compiles or loads the class, so a class freshly built can still have
            // it and one loaded from its file does not. It is left out on both sides, when the project's says
            // what the game's says.
            if (Template && Says)
            {
                const FString* Parent = Overrides.Find(Node.Path);
                if (Parent && SameInEffect(Root, Package + TEXT(".") + Node.Path, *Parent, Known))
                {
                    Says = false;
                    SaysTheParents.Add(Node.Path);
                }
                else
                    continue;
            }
            // Saved over the parent's template of the same name, it has that template's values. Saved over
            // the component class's default object (the editor names it ICH-<variable> when the class has a
            // component of that name of its own), it has them when the parent's template is the same in
            // effect. The editor itself keeps no template that is its parent's (it removes one when it
            // compiles or loads the class).
            if (!Says && !SaysTheParents.Contains(Node.Path) && !P4IsContentPackage(Game.Ref(Node.Template)))
            {
                const FString* Parent = Overrides.Find(Node.Path);
                Says = !Parent || !SameInEffect(Root, Package + TEXT(".") + Node.Path, *Parent, Known);
            }
            if (!Says)
                Empty.Add(Node.Path);
        }
        const auto DropRecords = [](FFile& File, const auto& Dropped)
        {
            for (FNode& Node : File.Nodes)
            {
                if (!P4SameText(Node.Class, TEXT("/Script/Engine.InheritableComponentHandler")))
                    continue;
                for (auto& Field : Node.Values.Fields)
                {
                    if (Field.first != "Records" || Field.second.Kind != FDecodedValue::EKind::Array)
                        continue;
                    auto& Items = Field.second.Items;
                    for (std::size_t Slot = Items.size(); Slot-- > 0;)
                    {
                        const FDecodedValue* Template = Items[Slot].Field("ComponentTemplate");
                        if (Template && Template->Kind == FDecodedValue::EKind::String &&
                            Dropped.Contains(Text(Template->Text)))
                            Items.erase(Items.begin() + Slot);
                    }
                }
            }
        };
        TSet<FString> MineDropped;
        for (const FNode& Node : Mine.Nodes)
        {
            const FNode* Theirs = SaysTheParents.Contains(Node.Path)
                ? Game.Nodes.FindByPredicate([&](const FNode& One) { return P4SameText(One.Path, Node.Path); })
                : nullptr;
            if (Theirs && P4SameDecodedValue(Theirs->Values, Node.Values))
            {
                MineDropped.Add(Node.Path);
                MineLeft.Add(Node.Path);
            }
        }
        DropRecords(Game, Empty);
        DropRecords(Mine, MineDropped);
        for (const FString& Path : Empty)
            GameLeft.Add(Path);
        EmptyTemplates = Empty.Num();
    }
    Records(Game, GameLeft);
    Records(Mine, MineLeft);
    Result.EmptyTemplates = EmptyTemplates;
    Result.TemplatesKnown = true;
    // A node of a construction script is numbered by when the editor made it, and the editor gives its
    // variable an id of its own. A node is told apart by the name of its variable, lists of nodes are
    // told in one order, and the ids are left out.
    int32 Ids = 0;
    // Fold: object paths and names in small letters, as the engine tells them apart (without regard to
    // case). The editor spells a name as it was first made in the session, so a class's own component
    // HeightMap can be saved as HEIGHTMAP when that name was made in capitals before. Strings stay exact.
    const auto Data = [&](const FFile& File, const TSet<FString, FLocKeySetFuncs>& Left, bool Fold)
    {
        TArray<FString> Content, EngineMade, Out;
        Lines(File, Content, EngineMade, nullptr, Fold);
        TSet<FString, FLocKeySetFuncs> FoldedLeft;
        if (Fold)
            for (const FString& Path : Left)
                FoldedLeft.Add(Path.ToLower());
        const TSet<FString, FLocKeySetFuncs>& Skip = Fold ? FoldedLeft : Left;
        // The terrain tags of a collision component are left out of what a level is compared by, because
        // a level's are the game's code's to make. A class's template has them as values of its own.
        for (const FString& Line : EngineMade)
            if (Line.Contains(TEXT(" | c4terraintagcontainer = "), ESearchCase::CaseSensitive))
                Content.Add(Line);
        TArray<TPair<FString, FString>> Renames;
        for (const FNode& Node : File.Nodes)
        {
            const FDecodedValue* Name = P4SameText(Node.Class, TEXT("/Script/Engine.SCS_Node"))
                ? Node.Values.Field("InternalVariableName")
                : nullptr;
            int32 Dot = INDEX_NONE;
            if (Name && Name->Kind == FDecodedValue::EKind::String && Node.Path.FindLastChar(TEXT('.'), Dot))
            {
                const FString Key = Node.Path, Value = Node.Path.Left(Dot + 1) + TEXT("node of ") + Text(Name->Text);
                Renames.Emplace(Fold ? Key.ToLower() : Key, Fold ? Value.ToLower() : Value);
            }
        }
        Renames.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B)
            { return A.Key.Len() > B.Key.Len(); });
        for (FString Line : Content)
        {
            const int32 Bar = Line.Find(TEXT(" | "), ESearchCase::CaseSensitive);
            if (Skip.Contains(Bar == INDEX_NONE ? Line : Line.Left(Bar)))
                continue;
            if (Bar != INDEX_NONE && Line.Mid(Bar + 3).StartsWith(TEXT("variableguid = "), ESearchCase::CaseSensitive))
            {
                ++Ids;
                continue;
            }
            for (const TPair<FString, FString>& Pair : Renames)
            {
                int32 From = 0;
                while (
                    (From = Line.Find(Pair.Key, ESearchCase::CaseSensitive, ESearchDir::FromStart, From)) != INDEX_NONE)
                {
                    const int32 After = From + Pair.Key.Len();
                    const TCHAR Next = After < Line.Len() ? Line[After] : TEXT(' ');
                    if (FChar::IsAlnum(Next) || Next == TEXT('_'))
                    {
                        From = After;
                        continue;
                    }
                    Line = Line.Left(From) + Pair.Value + Line.Mid(After);
                    From += Pair.Value.Len();
                }
            }
            // The lists of a script's nodes, in one order. The names given above change the line's length, so
            // the field is found again.
            const int32 Named = Line.Find(TEXT(" | "), ESearchCase::CaseSensitive);
            const int32 Is = Line.Find(TEXT(" = ["), ESearchCase::CaseSensitive);
            const FString Key = Named != INDEX_NONE && Is > Named ? Line.Mid(Named + 3, Is - Named - 3) : FString();
            if ((Key == TEXT("allnodes") || Key == TEXT("rootnodes") || Key == TEXT("childnodes")) &&
                Line.EndsWith(TEXT("]"), ESearchCase::CaseSensitive))
            {
                TArray<FString> Items;
                Line.Mid(Is + 4, Line.Len() - Is - 5).ParseIntoArray(Items, TEXT(","), true);
                Items.Sort(
                    [](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
                Line = Line.Left(Is + 4) + FString::Join(Items, TEXT(",")) + TEXT("]");
            }
            Out.Add(Line);
        }
        return Out;
    };
    TArray<FString> GameSays = Data(Game, GameLeft, false);
    const TArray<FString> MineSays = Data(Mine, MineLeft, false);
    TArray<FString> GameKeys = Data(Game, GameLeft, true);
    const TArray<FString> MineKeys = Data(Mine, MineLeft, true);
    if (GameKeys.Num() != GameSays.Num() || MineKeys.Num() != MineSays.Num())
        return Refuse(EP4OriginalClassComparisonError::DecodeFailed, TEXT("class-compare-folded-lines-differ"));
    // A template of the class's own that the game saves over a parent's template that says nothing, the
    // project saves over the template that one is made from, because the import makes no template that says
    // nothing (GOtaHandSpinner_C.Ak_GEN_VARIABLE over GOtakaraBaseTerrainBox_C's, which says nothing over
    // GOtakaraBaseTerrainBoxNoAI_C's). The two are made from the same in effect when the game's own files
    // say so.
    const auto NormalizeTemplates = [&](TArray<FString>& Given, const TArray<FString>& Project)
    {
        FString Root = HeaderFile;
        FPaths::NormalizeFilename(Root);
        {
            const FString Tail = P4ContentRelativePath(Package) + TEXT(".uasset");
            Root = Root.EndsWith(Tail, ESearchCase::IgnoreCase) ? Root.LeftChop(Tail.Len()) : FString();
        }
        const FString Mark = FString(TEXT(" | ")) + MadeFrom;
        TMap<FString, FString> Froms, Known;
        for (const FString& Line : Project)
        {
            const int32 At = Line.Find(Mark, ESearchCase::CaseSensitive);
            if (At != INDEX_NONE)
                Froms.Add(Line.Left(At), Line.Mid(At + Mark.Len()));
        }
        for (FString& Line : Given)
        {
            const int32 At = Line.Find(Mark, ESearchCase::CaseSensitive);
            const FString* Other = At != INDEX_NONE ? Froms.Find(Line.Left(At)) : nullptr;
            if (!Other || P4SameText(*Other, Line.Mid(At + Mark.Len())) ||
                !SameInEffect(Root, Line.Mid(At + Mark.Len()), *Other, Known))
                continue;
            Line = Line.Left(At) + Mark + *Other;
        }
    };
    NormalizeTemplates(GameSays, MineSays);
    NormalizeTemplates(GameKeys, MineKeys);
#if P4_KADAN_TEST_SEAMS
    TraceClassParentKeys(GameKeys, MineKeys);
#endif
    const auto FactsDigest = [&](const TArray<FString>& Facts, FString& Out)
    {
        TArray<FString> Hashes;
        for (const FString& Fact : Facts)
        {
            FString Hash;
            if (!P4HashUtf8(Fact, Hash))
                return false;
            Hashes.Add(MoveTemp(Hash));
        }
        Hashes.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
        return P4HashUtf8(TEXT("class-default-facts-v1\t") + FString::FromInt(Hashes.Num()) + TEXT("\n") +
                FString::Join(Hashes, TEXT("\n")),
            Out);
    };
    if (!FactsDigest(GameKeys, Result.GameDefaultFactsDigest) || !FactsDigest(MineKeys, Result.DefaultFactsDigest))
    {
        Result.GameDefaultFactsDigest.Reset();
        Result.DefaultFactsDigest.Reset();
        Result.DefaultsReadable = false;
    }
    TSet<FString, FLocKeySetFuncs> InGame(GameKeys), InMine(MineKeys);
    // A native component's attachment to its parent is made by the class's constructor, and the game's file
    // can write it again on a Blueprint's copy where the editor's file leaves it to the constructor (equal
    // to what the copy is made from). Such a line is the project's too when the project's own object is
    // attached there and its file says no attachment of its own.
    const FString Attached = TEXT(" | attachparent = \"");
    TSet<FString> MineAttaches;
    for (const FString& Line : MineKeys)
    {
        const int32 At = Line.Find(Attached, ESearchCase::CaseSensitive);
        if (At != INDEX_NONE)
            MineAttaches.Add(Line.Left(At));
    }
    const auto AttachedByConstructor = [&](const FString& Line)
    {
        const int32 At = Line.Find(Attached, ESearchCase::CaseSensitive);
        if (At == INDEX_NONE || !Line.EndsWith(TEXT("\""), ESearchCase::CaseSensitive) ||
            MineAttaches.Contains(Line.Left(At)))
            return false;
        const USceneComponent* Live = FindObject<USceneComponent>(Loaded, *Line.Left(At));
        const USceneComponent* Parent = Live ? Live->GetAttachParent() : nullptr;
        if (!Parent || Parent->GetOutermost() != Loaded)
            return false;
        FString Path = Parent->GetPathName(Loaded);
        Path.ReplaceInline(TEXT(":"), TEXT("."));
        return Path.ToLower() == Line.Mid(At + Attached.Len(), Line.Len() - At - Attached.Len() - 1);
    };
    // Reports the exact canonical keys used for comparison.
    TArray<FString> OnlyGame, OnlyMine;
    const int32 Differences = ClassDefaultDifferences(
        Result, GameKeys, MineKeys, MineSays, InGame, InMine, AttachedByConstructor, FactsDigest, OnlyGame, OnlyMine);
    Result.Objects = Game.Nodes.Num();
    Result.ComparedKnown = true;
    Result.Compared = GameSays.Num();
    Result.VariableIds = Ids + RecordIds;
    Result.Differences = Differences;
    Result.Same = Differences == 0;
    Result.OnlyGameSays = MoveTemp(OnlyGame);
    Result.OnlyProjectSays = MoveTemp(OnlyMine);
    return Result;
}
} // namespace P4LevelContent

FP4StrictWorldClassInventory P4ReadStrictWorldClassInventory(
    const FString& Package, const FString& HeaderFile, const FString& BodyFile, bool Resumed)
{
    using namespace P4LevelContent;
    FP4StrictWorldClassInventory Result;
    Result.WorldPackage = Package;
    Result.Resumed = Resumed;
    const auto Refuse = [&](const FString& Why)
    {
        Result.Error = Why;
        return Result;
    };
    if (!IsInGameThread() || !P4IsValidContentPackage(Package) ||

        !FPaths::GetExtension(HeaderFile).Equals(TEXT("umap"), ESearchCase::IgnoreCase) ||
        !FPaths::IsSamePath(FPaths::ChangeExtension(HeaderFile, TEXT("uexp")), BodyFile))
        return Refuse(TEXT("strict-class-inventory-requires-current-world-source-pair"));
    TArray<uint8> Header, Body;
    FString Error;
    if (!P4ReadBoundedSourceFile(HeaderFile, 64 * 1024 * 1024, Header, Error) ||
        !P4ReadBoundedSourceFile(BodyFile, 64 * 1024 * 1024, Body, Error) || !Header.Num() || !Body.Num())
        return Refuse(Error.IsEmpty() ? TEXT("strict-class-inventory-source-empty") : Error);
    FFile File;
    File.Header.assign(Header.GetData(), Header.GetData() + Header.Num());
    File.Body.assign(Body.GetData(), Body.GetData() + Body.Num());
    if (!ReadNodes(File, Package, Error) || !Package.Equals(Text(File.Package.Package), ESearchCase::CaseSensitive))
        return Refuse(Error.IsEmpty() ? TEXT("strict-class-inventory-source-package-differs") : Error);
    if (!P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), Result.WorldSourceDigest))
        return Refuse(TEXT("strict-class-inventory-source-hash-failed"));
    UPackage* Saved = FindPackage(nullptr, *Package);
    if (!Saved)
        Saved = LoadPackage(nullptr, *Package, LOAD_NoWarn | LOAD_Quiet);
    UWorld* World = Saved ? UWorld::FindWorldInPackage(Saved) : nullptr;
    FP4OriginalRecord Record;
    if (!World || !P4ReadOriginalRecord(World, Record, Error) || Record.Kind != TEXT("level") || Record.ReferenceOnly ||
        Record.Fidelity != TEXT("exact") || Record.SourceDigest != Result.WorldSourceDigest ||
        Saved->GetMetaData()->HasValue(World, GameBackedKey))
        return Refuse(TEXT("strict-class-inventory-source-does-not-match-imported-world"));
    for (const FNode& Node : File.Nodes)
    {
        if (!P4IsContentPackage(Node.Class))
            continue;
        const FString Declared = FPackageName::ObjectPathToPackageName(Node.Class);
        if (!FPackageName::IsValidLongPackageName(Declared))
            return Refuse(TEXT("strict-class-inventory-class-identity-invalid: ") + Node.Class.Left(512));
        if (Declared != Package)
            Result.RequiredClassPackages.AddUnique(Declared);
        // A missing declared class remains in the inventory and cannot be admitted.
        int32 ParentDepth = 0;
        for (const UClass* Class = LoadObject<UClass>(nullptr, *Node.Class, nullptr, LOAD_NoWarn | LOAD_Quiet); Class;
            Class = Class->GetSuperClass())
        {
            if (++ParentDepth > 256)
                return Refuse(TEXT("strict-class-inventory-parent-depth-limit"));
            const FString ParentPackage = Class->GetOutermost()->GetName();
            if (!P4IsContentPackage(ParentPackage))
                break;
            if (ParentPackage != Package)
                Result.RequiredClassPackages.AddUnique(ParentPackage);
            if (Result.RequiredClassPackages.Num() > 4096)
                return Refuse(TEXT("strict-class-inventory-class-count-limit"));
        }
        if (Result.RequiredClassPackages.Num() > 4096)
            return Refuse(TEXT("strict-class-inventory-class-count-limit"));
    }
    Result.RequiredClassPackages.Sort();
    Result.Known = true;
    return Result;
}

bool P4ReadStrictNativeClassSource(
    const FString& Package, FP4KadanSourceBinding& Source, UClass*& Class, FString& Error)
{
    Class = nullptr;
    if (!P4KadanReadBoundSource(Package, Source, Error))
        return false;
    const auto Inventory = P4MapKit::ReadPackage(
        std::vector<uint8>(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num()),
        std::vector<uint8>(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num()));
    if (!Inventory.Error.Code.empty())
    {
        Error = TEXT("strict-class-source-package-unreadable");
        return false;
    }
    const FString Short = FPackageName::GetLongPackageAssetName(Package) + TEXT("_C");
    const P4MapKit::FPackageExport* Generated = nullptr;
    FString SourceMetaClass;
    // Without case, as the engine finds the class: the game's name table can spell the generated class
    // differently from its package (GOtaGaraGara's class is GOtaGaragara_C).
    for (const auto& Export : Inventory.Exports)
        if (Short.Equals(P4LevelContent::Text(Export.Name), ESearchCase::IgnoreCase))
        {
            std::string MetaClass;
            if (Generated || !Inventory.Reference(Export.Class, MetaClass) ||
                !P4LevelContent::Text(MetaClass).EndsWith(TEXT("GeneratedClass"), ESearchCase::CaseSensitive))
            {
                Error = TEXT("strict-class-source-generated-class-identity-invalid");
                return false;
            }
            std::string ExportPath;
            const int32 ExportIndex = int32(&Export - Inventory.Exports.data()) + 1;
            if (!Inventory.Reference(ExportIndex, ExportPath))
            {
                Error = TEXT("strict-class-source-generated-class-path-unreadable");
                return false;
            }
            FString CanonicalPath = P4LevelContent::Text(ExportPath);
            if (!CanonicalPath.StartsWith(TEXT("/")))
                CanonicalPath = Package + TEXT(".") + CanonicalPath;
            if (CanonicalPath != Package + TEXT(".") + Short)
            {
                Error = TEXT("strict-class-source-generated-class-path-differs");
                return false;
            }
            SourceMetaClass = P4LevelContent::Text(MetaClass);
            Generated = &Export;
        }
    std::string Parent;
    if (!Generated || !Generated->Super || !Inventory.Reference(Generated->Super, Parent) || Parent.empty())
    {
        Error = TEXT("strict-class-source-parent-unreadable");
        return false;
    }
    FString ParentPath = P4LevelContent::Text(Parent);
    if (!ParentPath.StartsWith(TEXT("/")))
        ParentPath = Package + TEXT(".") + ParentPath;
    Class = LoadObject<UClass>(nullptr, *(Package + TEXT(".") + Short), nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!IsValid(Class))
    {
        Class = nullptr;
        Error = TEXT("strict-class-source-generated-class-unavailable");
        return false;
    }
    // Object names are compared as the engine finds them, without case: the game's name table can spell a class
    // differently from a package that names it (WATERBOX and WaterBox are one class).
    if (!Class->GetClass()->GetPathName().Equals(SourceMetaClass, ESearchCase::IgnoreCase))
    {
        Class = nullptr;
        Error = TEXT("strict-class-source-live-metaclass-differs: ") + SourceMetaClass.Left(512);
        return false;
    }
    if (!Class->GetSuperClass() || !Class->GetOutermost()->GetName().Equals(Package, ESearchCase::CaseSensitive) ||
        !Class->GetSuperClass()->GetPathName().Equals(ParentPath, ESearchCase::IgnoreCase))
    {
        Class = nullptr;
        Error = TEXT("strict-class-source-live-parent-differs: ") + ParentPath.Left(512);
        return false;
    }
    return P4KadanSourceBindingCurrent(Source, Error);
}

FString P4CompareOriginalClass(const FString& HeaderFile, const FString& BodyFile)
{
    return P4OriginalClassComparisonJson(P4LevelContent::CompareClassNative(HeaderFile, BodyFile));
}

FP4OriginalClassComparisonResult P4CompareOriginalClassNative(const FString& HeaderFile, const FString& BodyFile)
{
    return P4LevelContent::CompareClassNative(HeaderFile, BodyFile);
}

FP4OriginalClassComparisonResult P4CompareOriginalClassSourceNative(
    const TArray<uint8>& Header, const TArray<uint8>& Body, const FString& CanonicalPackage)
{
    return P4LevelContent::CompareClassNative(FString(), FString(), CanonicalPackage, &Header, &Body);
}
