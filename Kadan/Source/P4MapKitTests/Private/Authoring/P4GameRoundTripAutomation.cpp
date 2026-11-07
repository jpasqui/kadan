// Every imported game package of the project cooked whole again, as a full export ships it, and compared byte for
// byte with the game's own file (the copy the import converted from the game's containers). A package that differs
// says where: which table, object or file part.
//
// P4_NATIVE_ROUNDTRIP_PATHS: content folders to take, ';' between them (default /Game). P4_NATIVE_ROUNDTRIP_LIMIT:
// at most this many packages (default 500). The report is Saved/P4MapKit/RoundTrip/report.json.
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "AssetRegistryModule.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "P4ExportDriver.h"
#include "P4LevelExport.h"
#include "P4OriginalRecord.h"
#include "P4KadanSourceCatalog.h"
#include "P4NativeConstructionPreview.h"
#include "P4ExportProcess.h"
#include "P4GameNames.h"
#include "P4StockTags.h"
#include "P4MapKitCore/package_compare.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
std::vector<uint8> Bytes(const FString& File)
{
    TArray<uint8> Read;
    if (!IFileManager::Get().FileExists(*File) || !FFileHelper::LoadFileToArray(Read, *File, FILEREAD_Silent))
        return {};
    return std::vector<uint8>(Read.GetData(), Read.GetData() + Read.Num());
}

P4MapKit::FPackageFiles Files(const FString& Base, const TCHAR* Head)
{
    P4MapKit::FPackageFiles Out;
    Out.Header = Bytes(Base + Head);
    Out.Body = Bytes(Base + TEXT(".uexp"));
    Out.Bulk = Bytes(Base + TEXT(".ubulk"));
    return Out;
}

// A container's chunk ids by their text, read from the tool's listing ("container  id  kind  [path]").
TMap<FString, FString> Chunks(const FString& Retoc, const FString& Utoc, FString& Error)
{
    TMap<FString, FString> Out;
    TArray<uint8> Listing;
    if (!P4RunExportTool(Retoc, {TEXT("list"), Utoc}, 600.0, Listing, Error))
        return Out;
    Listing.Add(0);
    TArray<FString> Lines;
    FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Listing.GetData()))).ParseIntoArrayLines(Lines);
    for (const FString& Line : Lines)
    {
        TArray<FString> Words;
        Line.ParseIntoArrayWS(Words);
        if (Words.Num() >= 3 && Words[1].Len() == 24)
            Out.Add(Words[1].ToLower(), Words[2]);
    }
    return Out;
}

// The id of a package's chunk of one kind as the listing writes it: the package id's bytes, the index, the kind.
FString ChunkId(uint64 Package, uint8 Kind, bool LittleEndian)
{
    FString Text;
    for (int32 Byte = 0; Byte < 8; ++Byte)
        Text += FString::Printf(TEXT("%02x"), uint32(Package >> (8 * (LittleEndian ? Byte : 7 - Byte))) & 0xff);
    return Text + FString::Printf(TEXT("000000%02x"), Kind);
}

std::vector<uint8> Chunk(const FString& Retoc, const FString& Utoc, const FString& Id, const FString& Work, FString& Error)
{
    const FString File = Work / (Id + TEXT(".bin"));
    TArray<uint8> Ignored;
    if (!P4RunExportTool(Retoc, {TEXT("get"), Utoc, Id, File}, 600.0, Ignored, Error))
        return {};
    std::vector<uint8> Read = Bytes(File);
    IFileManager::Get().Delete(*File);
    return Read;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameRoundTrip, "P4MapKit.Editor.GameRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameRoundTrip::RunTest(const FString&)
{
    // The export reads assets without what the editor previews of the game's native classes.
    FP4SuspendNativePreviews Previews;
    TArray<FString> Paths;
    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROUNDTRIP_PATHS")).ParseIntoArray(Paths, TEXT(";"));
    if (!Paths.Num())
        Paths.Add(TEXT("/Game"));
    const FString LimitText = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROUNDTRIP_LIMIT"));
    const int32 Limit = LimitText.IsEmpty() ? 500 : FCString::Atoi(*LimitText);
    const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("P4MapKit/RoundTrip"));
    const FString Stage = Folder / TEXT("stage");
    IFileManager::Get().DeleteDirectory(*Stage, false, true);
    IFileManager::Get().DeleteDirectory(*(Folder / TEXT("zen")), false, true);
    const FString Originals = P4KadanCatalogGameFolder() / TEXT("Carrot4/Content");

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    Registry.SearchAllAssets(true);
    FARFilter Filter;
    Filter.bRecursivePaths = true;
    for (const FString& Path : Paths)
        Filter.PackagePaths.Add(FName(*Path));
    TArray<FAssetData> Found;
    Registry.GetAssets(Filter, Found);
    TSet<FName> Packages;
    TArray<FAssetData> Assets;
    for (const FAssetData& Asset : Found)
        if (!Packages.Contains(Asset.PackageName) && Asset.IsUAsset())
        {
            Packages.Add(Asset.PackageName);
            Assets.Add(Asset);
        }
    Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });

    TArray<TSharedPtr<FJsonValue>> Rows;
    TArray<TPair<FString, TSharedPtr<FJsonObject>>> Cooked;
    TMap<FString, TArray<int32>> ByClass; // class: same, different, not cooked, no original
    int32 Taken = 0;
    for (const FAssetData& Data : Assets)
    {
        if (Taken >= Limit)
            break;
        const FString Package = Data.PackageName.ToString();
        if (!Package.StartsWith(TEXT("/Game/")))
            continue;
        UObject* Asset = Data.GetAsset();
        if (!Asset || !P4QueryOriginalAsset(Asset).IsOriginal())
            continue;
        ++Taken;
        const bool Map = Asset->IsA<UWorld>();
        const TCHAR* Head = Map ? TEXT(".umap") : TEXT(".uasset");
        const FString Class = Asset->GetClass()->GetName();
        TArray<int32>& Counts = ByClass.FindOrAdd(Class);
        Counts.SetNumZeroed(4);
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Package);
        Row->SetStringField(TEXT("class"), Class);
        const FString Original = Originals / Package.Mid(6);
        if (!IFileManager::Get().FileExists(*(Original + Head)))
        {
            Row->SetStringField(TEXT("result"), TEXT("no-original-file"));
            ++Counts[3];
            Rows.Add(MakeShared<FJsonValueObject>(Row));
            continue;
        }
        FString Base, Error;
        if (UWorld* Level = Cast<UWorld>(Asset))
        {
            // The game's tags the level keeps for its cook.
            int32 Tags = 0;
            ForEachObjectWithPackage(Level->GetOutermost(), [&](UObject* Object) {
                Tags += P4CountGameStockTags(Object);
                return true;
            });
            Row->SetNumberField(TEXT("game_tags_kept"), Tags);
        }
        const double Start = FPlatformTime::Seconds();
        if (!P4CookGameAssetWhole(Asset, Stage, Base, Error))
        {
            Row->SetStringField(TEXT("result"), TEXT("not-cooked"));
            Row->SetStringField(TEXT("error"), Error.Left(600));
            ++Counts[2];
            Rows.Add(MakeShared<FJsonValueObject>(Row));
            continue;
        }
        Row->SetNumberField(TEXT("cook_seconds"), FPlatformTime::Seconds() - Start);
        const P4MapKit::FPackageComparison Compared =
            P4MapKit::ComparePackages(Files(Original, Head), Files(Base, Head), 60);
        Row->SetStringField(TEXT("result"), Compared.Same() ? TEXT("same") : TEXT("different"));
        Row->SetBoolField(TEXT("header_same"), Compared.HeaderSame);
        Row->SetBoolField(TEXT("body_same"), Compared.BodySame);
        Row->SetBoolField(TEXT("bulk_same"), Compared.BulkSame);
        Row->SetNumberField(TEXT("exports_compared"), double(Compared.ExportsCompared));
        Row->SetNumberField(TEXT("exports_differing"), double(Compared.ExportsDiffering));
        if (!Compared.Error.empty())
            Row->SetStringField(TEXT("error"), UTF8_TO_TCHAR(Compared.Error.c_str()));
        TArray<TSharedPtr<FJsonValue>> Lines;
        for (const std::string& Line : Compared.Differences)
            Lines.Add(MakeShared<FJsonValueString>(UTF8_TO_TCHAR(Line.c_str())));
        Row->SetArrayField(TEXT("differences"), Lines);
        Row->SetNumberField(TEXT("differences_left_out"), double(Compared.Truncated));
        ++Counts[Compared.Same() ? 0 : 1];
        Rows.Add(MakeShared<FJsonValueObject>(Row));
        Cooked.Emplace(Package, Row);
    }

    // What ships: the cooked packages packed as the export packs them, each package's chunks against the game's
    // chunks of the same package id. The tables above are the legacy files' and say where a difference lies.
    TArray<FString> From;
    FFileHelper::LoadFileToStringArray(From, *(P4KadanCatalogGameFolder() / TEXT("converted-from.txt")));
    // P4_NATIVE_ROUNDTRIP_RETOC packs with another build of the tool than the import's.
    const FString OtherRetoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROUNDTRIP_RETOC"));
    const FString Retoc = !OtherRetoc.IsEmpty() ? OtherRetoc : From.Num() > 2 ? From[2] : FString();
    const FString GameUtoc = From.Num() > 1 ? From[1] / TEXT(P4_GAME_CONTAINER ".utoc") : FString();
    const FString OurUtoc = Folder / TEXT("roundtrip.utoc");
    int32 ZenSame = 0, ZenCompared = 0;
    FString ZenError;
    TArray<uint8> Ignored;
    for (const TCHAR* Suffix : {TEXT(".utoc"), TEXT(".ucas"), TEXT(".pak")})
        IFileManager::Get().Delete(*FPaths::ChangeExtension(OurUtoc, Suffix + 1));
    if (Cooked.Num() && IFileManager::Get().FileExists(*Retoc) && IFileManager::Get().FileExists(*GameUtoc) &&
        P4RunExportTool(Retoc,
            {TEXT("to-zen"), Stage, OurUtoc, TEXT("--version"), TEXT("UE4_26"), TEXT("--reference"), From[1]}, 600.0,
            Ignored, ZenError))
    {
        const TMap<FString, FString> Ours = Chunks(Retoc, OurUtoc, ZenError);
        const TMap<FString, FString> Game = Chunks(Retoc, GameUtoc, ZenError);
        for (const auto& Item : Cooked)
        {
            const uint64 Id = P4ExportPackageId(Item.Key);
            const bool Little = Ours.Contains(ChunkId(Id, 2, true));
            const FString Bundle = ChunkId(Id, 2, Little), Bulk = ChunkId(Id, 3, Little);
            if (!Ours.Contains(Bundle) || !Game.Contains(Bundle))
            {
                Item.Value->SetStringField(TEXT("zen"), Ours.Contains(Bundle) ? TEXT("no-game-chunk") : TEXT("not-packed"));
                continue;
            }
            ++ZenCompared;
            FString Why;
            const std::vector<uint8> A = Chunk(Retoc, GameUtoc, Bundle, Folder, Why);
            const std::vector<uint8> B = Chunk(Retoc, OurUtoc, Bundle, Folder, Why);
            const bool BulkBoth = Ours.Contains(Bulk) == Game.Contains(Bulk);
            const bool BulkSame = !Game.Contains(Bulk) ||
                (BulkBoth && Chunk(Retoc, GameUtoc, Bulk, Folder, Why) == Chunk(Retoc, OurUtoc, Bulk, Folder, Why));
            const bool Same = !A.empty() && A == B && BulkBoth && BulkSame;
            ZenSame += Same;
            Item.Value->SetStringField(TEXT("zen"), Same ? TEXT("same") : TEXT("different"));
            if (!Same)
            {
                std::size_t First = 0;
                while (First < A.size() && First < B.size() && A[First] == B[First])
                    ++First;
                Item.Value->SetStringField(TEXT("zen_detail"),
                    FString::Printf(TEXT("package %d bytes, ours %d, first difference at %d; bulk %s"), int32(A.size()),
                        int32(B.size()), int32(First), !BulkBoth ? TEXT("only one side") : BulkSame ? TEXT("same") : TEXT("differs")));
                // P4_NATIVE_ROUNDTRIP_KEEP=1 keeps both packages of a differing one, to look at.
                if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROUNDTRIP_KEEP")) == TEXT("1"))
                {
                    const FString Kept = Folder / TEXT("zen") / FPackageName::GetShortName(Item.Key);
                    FFileHelper::SaveArrayToFile(TArrayView<const uint8>(A.data(), int32(A.size())), *(Kept + TEXT(".game.bin")));
                    FFileHelper::SaveArrayToFile(TArrayView<const uint8>(B.data(), int32(B.size())), *(Kept + TEXT(".ours.bin")));
                }
            }
            if (!Why.IsEmpty())
                Item.Value->SetStringField(TEXT("zen_error"), Why.Left(300));
        }
    }

    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("originals"), Originals);
    Report->SetStringField(TEXT("retoc"), Retoc);
    Report->SetNumberField(TEXT("packages"), Taken);
    auto Classes = MakeShared<FJsonObject>();
    int32 Same = 0;
    for (const auto& Pair : ByClass)
    {
        auto Counts = MakeShared<FJsonObject>();
        Counts->SetNumberField(TEXT("same"), Pair.Value[0]);
        Counts->SetNumberField(TEXT("different"), Pair.Value[1]);
        Counts->SetNumberField(TEXT("not_cooked"), Pair.Value[2]);
        Counts->SetNumberField(TEXT("no_original_file"), Pair.Value[3]);
        Classes->SetObjectField(Pair.Key, Counts);
        Same += Pair.Value[0];
    }
    Report->SetNumberField(TEXT("same"), Same);
    Report->SetNumberField(TEXT("zen_compared"), ZenCompared);
    Report->SetNumberField(TEXT("zen_same"), ZenSame);
    Report->SetStringField(TEXT("zen_error"), ZenError);
    Report->SetObjectField(TEXT("by_class"), Classes);
    Report->SetArrayField(TEXT("rows"), Rows);
    // P4_NATIVE_ROUNDTRIP_PROBE=Class;Property;...: each loaded object of that class with those properties as text,
    // beside its archetype's, to see what an import left on an object the cook then writes.
    TArray<FString> Probe;
    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROUNDTRIP_PROBE")).ParseIntoArray(Probe, TEXT(";"));
    if (Probe.Num() > 1)
    {
        TArray<TSharedPtr<FJsonValue>> Seen;
        for (TObjectIterator<UObject> It; It && Seen.Num() < 400; ++It)
        {
            if (It->GetClass()->GetName() != Probe[0] || It->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
                continue;
            auto One = MakeShared<FJsonObject>();
            One->SetStringField(TEXT("object"), It->GetPathName());
            One->SetStringField(TEXT("flags"), FString::Printf(TEXT("%08x"), uint32(It->GetFlags())));
            const UObject* Template = It->GetArchetype();
            One->SetStringField(TEXT("archetype"), Template ? Template->GetPathName() : FString());
            for (int32 Index = 1; Index < Probe.Num(); ++Index)
                if (const FProperty* Property = It->GetClass()->FindPropertyByName(FName(*Probe[Index])))
                {
                    FString Mine, Theirs;
                    Property->ExportTextItem(Mine, Property->ContainerPtrToValuePtr<void>(*It), nullptr, nullptr, PPF_None);
                    if (Template)
                        Property->ExportTextItem(
                            Theirs, Property->ContainerPtrToValuePtr<void>(Template), nullptr, nullptr, PPF_None);
                    One->SetStringField(Probe[Index], Mine.Left(2000));
                    One->SetStringField(Probe[Index] + TEXT(" (archetype)"), Theirs.Left(2000));
                }
            Seen.Add(MakeShared<FJsonValueObject>(One));
        }
        Report->SetArrayField(TEXT("probe"), Seen);
    }
    FString Text;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text));
    TestTrue(TEXT("The round-trip report is written"),
        FFileHelper::SaveStringToFile(Text, *(Folder / TEXT("report.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(TEXT("Game packages were compared"), Taken > 0);
    AddInfo(FString::Printf(TEXT("%d of %d game packages are byte identical after a whole cook; %d of %d packed ones"),
        Same, Taken, ZenSame, ZenCompared));
    return true;
}
#endif
