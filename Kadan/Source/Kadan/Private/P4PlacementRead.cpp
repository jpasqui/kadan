// Placement export: the game's placement lists, read, and their options given to imported actors.
#include "P4PlacementInternal.h"
#include "P4KadanSourceCatalog.h"
#include "AssetRegistryModule.h"

using namespace P4PlacementDetail;

namespace
{
uint64 NumberOf(const FDecodedValue* Value)
{
    if (!Value || Value->Kind != FDecodedValue::EKind::Number || Value->Text.empty() || Value->Text[0] == '-')
        return 0;
    return uint64(std::strtoull(Value->Text.c_str(), nullptr, 10));
}

bool BytesOf(const FDecodedValue* Value, TArray<uint8>& Out)
{
    Out.Reset();
    if (!Value)
        return true;
    if (Value->Kind != FDecodedValue::EKind::Array)
        return false;
    for (const FDecodedValue& Item : Value->Items)
    {
        const uint64 Byte = NumberOf(&Item);
        if (Item.Kind != FDecodedValue::EKind::Number || Byte > 255)
            return false;
        Out.Add(uint8(Byte));
    }
    return true;
}

// The part of a list's or a level's name after the map's: "_Teki" of AP_Cave000_F00_P_Teki and of Cave000_F00_Teki.
FString ListPart(const FString& List)
{
    const FString Leaf = List.StartsWith(TEXT("AP_")) ? List.Mid(3) : List;
    const int32 At = Leaf.Find(TEXT("_P"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (At == INDEX_NONE || (At + 2 < Leaf.Len() && Leaf[At + 2] != TEXT('_')))
        return FString();
    return Leaf.Mid(At + 2);
}

FString ListBase(const FString& List)
{
    const FString Leaf = List.StartsWith(TEXT("AP_")) ? List.Mid(3) : List;
    const int32 At = Leaf.Find(TEXT("_P"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    return At == INDEX_NONE ? FString() : Leaf.Left(At);
}
} // namespace

// ---------------------------------------------------------------------- the game's rows, given on import

bool P4ReadGamePlacementLists(const FString& Folder, TArray<FP4GamePlacementRow>& Rows, FString& Error)
{
    Rows.Reset();
    Error.Reset();
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Folder / TEXT("AP_*.uasset")), true, false);
    Files.Sort();
    for (const FString& File : Files)
    {
        const FString List = FPaths::GetBaseFilename(File), Header = Folder / File;
        TArray<uint8> Head, Body;
        if (!FFileHelper::LoadFileToArray(Head, *Header, FILEREAD_Silent) ||
            !FFileHelper::LoadFileToArray(Body, *FPaths::ChangeExtension(Header, TEXT("uexp")), FILEREAD_Silent))
        {
            Error = List + TEXT(" cannot be read");
            return false;
        }
        const std::vector<std::uint8_t> HeadBytes(Head.GetData(), Head.GetData() + Head.Num()),
            BodyBytes(Body.GetData(), Body.GetData() + Body.Num());
        const P4MapKit::FPackageInventory Package = P4MapKit::ReadPackage(HeadBytes, BodyBytes);
        if (!Package.Error.Code.empty())
        {
            Error = List + TEXT(" is not a package Kadan reads (") + Wide(Package.Error.Code) + TEXT(")");
            return false;
        }
        bool Found = false;
        for (const auto& Export : Package.Exports)
        {
            std::string Class;
            if (Export.Outer != 0 || !Package.Reference(Export.Class, Class) ||
                Class != P4_GAME_SCRIPT_PACKAGE ".ActorPlacementInfoAsset")
                continue;
            P4MapKit::FPropertyDecoder Decoder(Package, BodyBytes);
            std::size_t Position = Export.Offset;
            FDecodedValue Values;
            if (Export.Offset > BodyBytes.size() || Export.Size > BodyBytes.size() - Export.Offset ||
                !Decoder.Properties(Position, Export.Offset + Export.Size, Values))
            {
                Error = List + TEXT("'s rows cannot be read (") + Wide(Decoder.Error.Code) + TEXT(")");
                return false;
            }
            Found = true;
            const FDecodedValue* Generators = Values.Field("ActorGeneratorList");
            for (std::size_t Index = 0; Generators && Index < Generators->Items.size(); ++Index)
            {
                const FDecodedValue& Item = Generators->Items[Index];
                FP4GamePlacementRow Row;
                Row.List = List;
                const FDecodedValue* Generate = Item.Field("GenerateInfo");
                Row.Id = NumberOf(Generate ? Generate->Field("DebugUniqueId") : nullptr);
                Row.AssetVersion = NumberOf(Item.Field("AssetVersion"));
                Row.GeneratorVersion = NumberOf(Item.Field("GeneratorVersion"));
                if (const FDecodedValue* Class_ = Item.Field("SoftRefActorClass"))
                    Row.Class = Wide(Class_->Text);
                if (const FDecodedValue* Serialize = Item.Field("ActorSerializeParameter"))
                    for (const auto& Pair : Serialize->Fields)
                        for (const char* Half : {"Static", "Dynamic"})
                        {
                            TArray<uint8> Bytes;
                            if (!BytesOf(Pair.second.Field(Half), Bytes))
                            {
                                Error = FString::Printf(TEXT("%s row %d: %s.%s is not a byte array"), *List,
                                    int32(Index), *Wide(Pair.first), UTF8_TO_TCHAR(Half));
                                return false;
                            }
                            if (Bytes.Num())
                                Row.Blobs.Add(Wide(Pair.first) + TEXT(".") + UTF8_TO_TCHAR(Half), MoveTemp(Bytes));
                        }
                Rows.Add(MoveTemp(Row));
            }
        }
        if (!Found)
        {
            Error = List + TEXT(" holds no placement list");
            return false;
        }
    }
    return true;
}

FString P4GamePlacementFolder(const FString& GameRoot, const FString& LevelPackage)
{
    if (GameRoot.IsEmpty() || !P4IsContentPackage(LevelPackage))
        return FString();
    // A map's lists are in ActorPlacementInfo beside the map, and its sublevels in Sublevels beside it.
    FString Map = FPackageName::GetLongPackagePath(LevelPackage);
    if (FPaths::GetCleanFilename(Map) == TEXT("Sublevels"))
        Map = FPaths::GetPath(Map);
    // A level reader passes its mount-relative Content root. Container callers pass the
    // retoc output folder, whose game mount is Carrot4/Content.
    FString Found;
    for (const FString& Root : {GameRoot, GameRoot / TEXT(P4_GAME_CONTENT_FOLDER)})
    {
        const FString Folder = FPaths::Combine(Root, P4ContentRelativePath(Map), TEXT("ActorPlacementInfo"));
        if (!IFileManager::Get().DirectoryExists(*Folder))
            continue;
        if (!Found.IsEmpty())
            return FString(); // Do not silently choose between duplicate mounts.
        Found = Folder;
    }
    return Found;
}

FString P4MapPlacementFolder(const FString& GameFolder, const FString& LevelPackage)
{
    const FString Folder = GameFolder.IsEmpty() ? FString() : P4GamePlacementFolder(GameFolder, LevelPackage);
    return Folder.IsEmpty() ? P4GamePlacementFolder(P4KadanCatalogGameFolder(), LevelPackage) : Folder;
}

const FP4GamePlacementRow* P4GamePlacementRowOf(
    const AActor* Actor, const TArray<FP4GamePlacementRow>& Rows, FString* Why)
{
    if (!Actor)
    {
        if (Why)
            *Why = TEXT("it has no placement number");
        return nullptr;
    }
    return P4GamePlacementRowFor(PlacementNumber(Actor), LevelLeaf(Actor), Actor->GetClass()->GetPathName(), Rows, Why);
}

const FP4GamePlacementRow* P4GamePlacementRowFor(
    uint64 Id, const FString& Level, const FString& Class, const TArray<FP4GamePlacementRow>& Rows, FString* Why)
{
    const auto Say = [&](const FString& Text)
    {
        if (Why)
            *Why = Text;
        return nullptr;
    };
    if (!Id)
        return Say(TEXT("it has no placement number"));
    TArray<const FP4GamePlacementRow*> Found;
    for (const FP4GamePlacementRow& Row : Rows)
        if (Row.Id == Id)
            Found.Add(&Row);
    if (!Found.Num())
        return Say(FString::Printf(TEXT("no list of the map has its number %llu"), Id));
    // Several lists of a map can have the same number (a day list and the list of an update): the list of the
    // actor's own level is the one, by the part of their names after the map's.
    if (Found.Num() > 1)
    {
        TArray<const FP4GamePlacementRow*> Own;
        for (const FP4GamePlacementRow* Row : Found)
        {
            const FString Base = ListBase(Row->List);
            if (!Base.IsEmpty() && Level.StartsWith(Base, ESearchCase::CaseSensitive) &&
                Level.Mid(Base.Len()) == ListPart(Row->List))
                Own.Add(Row);
        }
        if (Own.Num() == 1)
            Found = Own;
    }
    for (int32 I = 1; I < Found.Num(); ++I)
        if (!Found[I]->Class.Equals(Found[0]->Class, ESearchCase::IgnoreCase) ||
            !Found[I]->Blobs.OrderIndependentCompareEqual(Found[0]->Blobs) ||
            Found[I]->AssetVersion != Found[0]->AssetVersion ||
            Found[I]->GeneratorVersion != Found[0]->GeneratorVersion)
            return Say(FString::Printf(TEXT("the lists %s and %s have its number %llu with other values"),
                *Found[0]->List, *Found[I]->List, Id));
    if (!Found[0]->Class.Equals(Class, ESearchCase::IgnoreCase))
        return Say(TEXT("its row in ") + Found[0]->List + TEXT(" is of the class ") + Found[0]->Class);
    if (Why)
        Why->Reset();
    return Found[0];
}

FString P4GamePlacementListPackage(const FString& LevelPackage, const FString& List)
{
    FString Map = FPackageName::GetLongPackagePath(LevelPackage);
    if (FPaths::GetCleanFilename(Map) == TEXT("Sublevels"))
        Map = FPaths::GetPath(Map);
    return Map / TEXT("ActorPlacementInfo") / List;
}

bool P4GameRowsOfLevelEdit(UWorld* World, const FP4LevelChangesResult& Changes, const TArray<FP4GamePlacementRow>* Rows,
    TMap<FString, FP4GameRowEdit>& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel)
    {
        Error = TEXT("The selected level cannot be opened.");
        return false;
    }
    const FString Package = World->GetOutermost()->GetName(), Level = FPackageName::GetLongPackageAssetName(Package);
    // An actor of the game's level that the edit moves, changes or removes, and what it is: its placement number
    // and its class.
    struct FCandidate
    {
        FString Actor, Class;
        uint64 Id = 0;
        bool Removed = false;
    };
    TArray<FCandidate> Candidates;
    const auto Present = [&](const FString& Name, bool Removed)
    {
        const AActor* Actor = FindObject<AActor>(World->PersistentLevel, *Name);
        if (!P4IsGameplayActor(Actor))
            return;
        Candidates.Add({Name, Actor->GetClass()->GetPathName(), PlacementNumber(Actor), Removed});
    };
    for (const FP4LevelActorMove& Move : Changes.Moved)
        Present(Move.Actor, false);
    for (const FP4LevelActorChanges& Changed : Changes.OtherChanged)
        Present(Changed.Actor, false);
    for (const FString& Name : Changes.Removed)
    {
        const FP4RemovedActor* Gone = Changes.RemovedActors.Find(Name);
        if (Gone && Gone->PlacementNumber)
            Candidates.Add({Name, Gone->Class, Gone->PlacementNumber, true});
    }
    for (const FCandidate& Candidate : Candidates)
    {
        if (!Candidate.Id)
            continue;
        if (!Rows)
        {
            // The lists this project has imported name the files to read again from the game.
            TArray<FAssetData> Lists;
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
                .Get()
                .GetAssetsByPath(FName(*FPackageName::GetLongPackagePath(P4GamePlacementListPackage(Package, TEXT("X")))),
                    Lists);
            TArray<FString> Names;
            for (const FAssetData& List : Lists)
                Names.AddUnique(List.AssetName.ToString());
            Names.Sort();
            Error = FString::Printf(
                TEXT(
                    "%s of %s is one of the game's gameplay objects, which the game builds from its placement lists, and Kadan has no copy of the game's files of those lists. Choose the game folder in the import settings. If it is chosen, tick %s on the Import page and wait for the count, which reads them from the game's files, then review the export again."),
                *Candidate.Actor, *Level,
                Names.Num() ? *FString::Join(Names, TEXT(", ")) : TEXT("the map's placement lists (AP_)"));
            return false;
        }
        // A gameplay object with no row of the game's lists is one the game keeps in the level: a level change.
        const FP4GamePlacementRow* Row = P4GamePlacementRowFor(Candidate.Id, Level, Candidate.Class, *Rows);
        if (!Row)
            continue;
        FP4GameRowEdit Edit;
        Edit.Actor = Candidate.Actor;
        Edit.List = P4GamePlacementListPackage(Package, Row->List);
        Edit.Id = Row->Id;
        Edit.Removed = Candidate.Removed;
        Out.Add(Candidate.Actor, MoveTemp(Edit));
    }
    return true;
}

FP4PlacementOptionsReport P4ApplyGamePlacementOptions(UWorld* World, const TArray<FP4GamePlacementRow>& Rows)
{
    FP4PlacementOptionsReport Report;
    if (!IsInGameThread() || !World || !World->PersistentLevel)
        return Report;
    const TArray<AActor*> Actors = World->PersistentLevel->Actors;
    for (AActor* Actor : Actors)
    {
        if (!IsValid(Actor) || !Actor->GetClass()->FindPropertyByName(TEXT("GenerateInfo")))
            continue;
        FString Why;
        const FP4GamePlacementRow* Row = P4GamePlacementRowOf(Actor, Rows, &Why);
        if (!Row)
        {
            Report.WithoutRow.Add(Actor->GetName() + TEXT(": ") + Why);
            continue;
        }
        ++Report.Rows;
        const P::Context Context = ContextOf(Actor);
        const FString Class = Actor->GetClass()->GetName();
        const int32 Before = Report.Values;
        TArray<FString> Keys;
        Row->Blobs.GetKeys(Keys);
        Keys.Sort();
        for (const FString& Key : Keys)
        {
            if (NotAnOption(Key))
                continue;
            P::Blob Blob;
            FString WhyNot;
            if (!ReadRowBlob(*Row, Key, Context, Blob, WhyNot))
            {
                ++Report.NotRead.FindOrAdd(Class + TEXT(" ") + Key + TEXT(" (") + WhyNot + TEXT(")"));
                continue;
            }
            if (!Blob.Exact())
                ++Report.NotRead.FindOrAdd(Class + TEXT(" ") + Key + TEXT(" (read in part)"));
            const P::SlotName* Slot = SlotNamed(Key);
            UObject* Owner = OwnerOf(Actor, *Slot);
            if (!Owner)
            {
                Report.NotHeld.Add(Actor->GetName() + TEXT(" has no component for ") + Key);
                continue;
            }
            const std::vector<P::Field> Fields = LayoutFields(Slot->S, Context, Blob.Variant);
            for (const auto& Member : Blob.Root.Members)
            {
                const P::Field* Layout = FieldNamed(Fields, Member.first);
                if (Layout && NotAProperty(*Layout))
                    continue;
                const FString Field = Wide(Member.first);
                const P::Type* Type = Layout ? Layout->Of : nullptr;
                FProperty* Leaf = nullptr;
                void* Data = nullptr;
                FString Error;
                if (!Type || !ResolvePlacementField(Owner, Actor, Field, Leaf, Data, Error))
                {
                    Report.NotHeld.Add(Actor->GetName() + TEXT(" ") + Key + TEXT(" ") + Field + TEXT(": ") +
                        (Error.IsEmpty() ? FString(TEXT("no such field")) : Error));
                    continue;
                }
                P::Value Now;
                if (ToValue(Leaf, Data, *Type, Now, Error) &&
                    Canonical(Now, std::string()) == Canonical(Member.second, std::string()))
                    continue;
                Error.Reset();
                // Written into a copy first, so that a value that cannot be given leaves the property as it was.
                void* Copy = FMemory::Malloc(FMath::Max(Leaf->ElementSize, 1), Leaf->GetMinAlignment());
                Leaf->InitializeValue(Copy);
                Leaf->CopySingleValue(Copy, Data);
                const bool Given = FromValue(Leaf, Copy, *Type, Member.second, Error);
                if (Given)
                    Leaf->CopySingleValue(Data, Copy);
                Leaf->DestroyValue(Copy);
                FMemory::Free(Copy);
                if (Given)
                    ++Report.Values;
                else
                    Report.NotHeld.Add(Actor->GetName() + TEXT(" ") + Key + TEXT(" ") + Field + TEXT(": ") + Error);
            }
        }
        if (Report.Values > Before)
            Report.Changed.Add(Actor);
    }
    return Report;
}
