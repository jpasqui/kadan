#include "P4MaterialRecovery.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4MaterialUsedResourcesInternal.h"
#include "P4KadanImport.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "P4MaterialInstanceComparison.h"
#include "P4OriginalImportSave.h"
#include "P4KadanImportSources.h"
#include "P4NativeHash.h"
#include "P4OriginalWorldSource.h"
#include "P4OriginalLandscapePreview.h"
#include "P4KadanContainerIndex.h"
#include "P4BoundedSourceRead.h"
#include "P4KadanOriginalConversion.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "LandscapeComponent.h"
#include "LandscapeMaterialInstanceConstant.h"
#include "StaticParameterSet.h"
#include "MaterialShared.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Engine/Texture.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "P4MapKitCore/material_canon_game.h"
#include "P4MapKitCore/native_custom_backend_archive.h"
#include "P4MapKitCore/landscape_color_preview.h"

namespace
{
namespace M = P4MapKit::Material;
using P4MapKit::CustomMaterial::Bytes;
using P4MapKit::CustomMaterial::NativeBackend::ShaderArchive;

std::string Utf8(const FString& Text)
{
    FTCHARToUTF8 Value(*Text);
    return std::string(Value.Get(), Value.Length());
}
FString Wide(const std::string& Text)
{
    return FString(UTF8_TO_TCHAR(Text.c_str()));
}

// A texture node whose texture is engine content (M_stg_Cave_Sky_Opaque_00_Master's default cube is
// /Engine/EngineResources/DefaultTextureCube_Low) has no game source to read its sampler type from. The
// engine asset is the same in every editor: its sampler type is the one the editor gives that asset.
void TypeEngineTextureSamplers(M::Json& Graph)
{
    if (!Graph.Has("nodes"))
        return;
    for (auto& Node : Graph["nodes"].object)
    {
        if (!Node.second.Has("props"))
            continue;
        M::Json& Props = Node.second["props"];
        if (Props.Has("SamplerType") || !Props.Has("Texture") || Props.At("Texture").kind != M::Json::String)
            continue;
        const FString Path = Wide(Props.At("Texture").Text());
        if (!Path.StartsWith(TEXT("/Engine/"), ESearchCase::CaseSensitive))
            continue;
        const UTexture* Texture = LoadObject<UTexture>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Texture)
            continue;
        const EMaterialSamplerType Type = UMaterialExpressionTextureBase::GetSamplerTypeForTexture(Texture);
        Props["SamplerType"] = M::Json(Utf8(StaticEnum<EMaterialSamplerType>()->GetNameStringByValue(int64(Type))));
    }
}

bool ReadBytes(const FString& File, int64 Limit, Bytes& Out)
{
    const int64 Size = IFileManager::Get().FileSize(*File);
    TArray<uint8> Data;
    if (Size <= 0 || Size > Limit || !FFileHelper::LoadFileToArray(Data, *File, FILEREAD_Silent))
        return false;
    Out.assign(Data.GetData(), Data.GetData() + Data.Num());
    return true;
}

FP4MaterialRecoveryResult Refuse(const FString& Kind, const FString& Reason, const FString& Name)
{
    FP4MaterialRecoveryResult Result;
    Result.Kind = Kind;
    Result.Reason = Reason;
    Result.RefusalName = Name.IsEmpty() ? Reason : Name;
    return Result;
}

// The game's shader library in an extracted game: Carrot4/Content/ShaderArchive-Carrot4-<platform>.ushaderbytecode.
FString FindShaderLibrary(const FString& GameFolder)
{
    TArray<FString> Found;
    IFileManager::Get().FindFilesRecursive(Found, *GameFolder, TEXT(P4_GAME_SHADER_ARCHIVE_ANY_FORMAT), true, false);
    Found.Sort();
    return Found.Num() == 1 ? Found[0] : FString();
}
} //namespace

struct FP4MaterialRecovery::FState
{
    static TArray<FState*>& Live()
    {
        static TArray<FState*> States;
        return States;
    }
    FState()
    {
        check(IsInGameThread());
        Live().Add(this);
    }
    ~FState()
    {
        check(IsInGameThread());
        Live().Remove(this);
    }
    FString GameFolder, ShaderArchiveFile;
    TMap<FString, TPair<FString, FString>> Files; // package, lower case: header and body
    // The rows Files was made from (the last one per package, as in Files): the sources this recovery reads.
    TMap<FString, TSharedPtr<FP4KadanSourceAsset>> Rows;
    std::vector<std::string> Instances;
    struct FSharedArchive
    {
        bool bArchiveRead = false;
        int32 ReadAttempts = 0, FamilyBuilds = 0;
        FString ArchiveProblem;
        std::shared_ptr<const Bytes> ArchiveBytes;
        std::unique_ptr<ShaderArchive> Archive;
        M::MaterialOriginalArchiveAccess Access;
        FString ArchiveSha256;
        int32 ArchiveDigestComputeCount = 0;
        // A recovered master family, the instance list it was recovered with and every package
        // its recovery could have read (by the bytes read, empty when absent). A material import
        // makes a new recovery for each material of a family, and recovering the same master
        // again from the same bytes is most of a cold import's material time.
        struct FFamilyMemo
        {
            std::vector<std::string> Instances;
            std::map<std::string, std::string> Reads;
            M::MaterialMasterFamily Family;
        };
        std::map<std::string, std::vector<FFamilyMemo>> FamilyMemos;
    };
    TSharedPtr<FSharedArchive> SharedArchive = MakeShared<FSharedArchive>();
    M::MaterialMasterSources Sources;
    std::map<std::string, M::MaterialMasterFamily> Families; // by the master's package, lower case
    TArray<FString> PreviewDiffers;
    // Every package this recovery's sources read: the digest of its bytes, empty when it was absent.
    mutable std::map<std::string, std::string> Reads;

    static std::string ReadDigest(const Bytes& Header, const Bytes& Body)
    {
        FSHA1 Hash;
        const uint64 Sizes[2] = {uint64(Header.size()), uint64(Body.size())};
        Hash.Update(reinterpret_cast<const uint8*>(Sizes), sizeof(Sizes));
        if (!Header.empty())
            Hash.Update(Header.data(), Header.size());
        if (!Body.empty())
            Hash.Update(Body.data(), Body.size());
        Hash.Final();
        uint8 Digest[20];
        Hash.GetHash(Digest);
        return std::string(reinterpret_cast<const char*>(Digest), 20);
    }
    // True when every package the memo's recovery read reads the same bytes (or is absent) here.
    bool ReadsSame(const std::map<std::string, std::string>& Prior) const
    {
        for (const auto& Read : Prior)
        {
            M::MaterialOriginalImportRawAsset Raw;
            if (!RawAsset(Read.first, Raw))
            {
                if (!Read.second.empty())
                    return false;
                continue;
            }
            if (Read.second.empty() || ReadDigest(*Raw.Header, *Raw.Body) != Read.second)
                return false;
        }
        return true;
    }

    bool OpenArchive()
    {
        auto& bArchiveRead = SharedArchive->bArchiveRead;
        auto& ArchiveProblem = SharedArchive->ArchiveProblem;
        auto& ArchiveBytes = SharedArchive->ArchiveBytes;
        auto& Archive = SharedArchive->Archive;
        auto& Access = SharedArchive->Access;
        if (bArchiveRead)
            return Archive != nullptr;
        bArchiveRead = true;
        ++SharedArchive->ReadAttempts;
        const FString File = ShaderArchiveFile.IsEmpty() ? FindShaderLibrary(GameFolder) : ShaderArchiveFile;
        if (File.IsEmpty())
        {
            ArchiveProblem = TEXT(
                "The game's shader library (" P4_GAME_CONTENT_FOLDER "/" P4_GAME_SHADER_ARCHIVE_ANY_FORMAT
                ", one file) is not in the game folder. Extract it with the packages.");
            return false;
        }
        auto Data = std::make_shared<Bytes>();
        if (!ReadBytes(File, int64(1) << 31, *Data))
        {
            ArchiveProblem = TEXT("The game's shader library cannot be read: ") + File;
            return false;
        }
        try
        {
            ArchiveBytes = Data;
            Archive.reset(new ShaderArchive(*ArchiveBytes));
        }
        catch (const std::exception& Error)
        {
            ArchiveProblem = TEXT("The game's shader library is not one this reads: ") + Wide(Error.what());
            Archive.reset();
            return false;
        }
        ShaderArchive* Library = Archive.get();
        Access.MapIndices = [Library](const std::array<std::uint8_t, 20>& Hash)
        {
            const auto Found = Library->Map(Bytes(Hash.begin(), Hash.end()));
            return std::vector<std::uint32_t>(Found.begin(), Found.end());
        };
        Access.Payload = [Library](std::uint32_t Index) { return Library->Payload(Index); };
        Access.Frequency = [Library](std::uint32_t Index) { return std::uint8_t(Library->Frequency(Index)); };
        return true;
    }

    bool RawAsset(const std::string& Package, M::MaterialOriginalImportRawAsset& Out) const
    {
        const FString Name = Wide(Package);
        FString Header, Body;
        if (const auto* Found = Files.Find(Name.ToLower()))
        {
            Header = Found->Key;
            Body = Found->Value;
        }
        else if (P4IsContentPackageIgnoringCase(Name))
        {
            for (const TCHAR* Root : {TEXT(P4_GAME_CONTENT_FOLDER), TEXT("Content")})
            {
                const FString Stem = GameFolder / Root / P4ContentRelativePath(Name);
                if (IFileManager::Get().FileExists(*(Stem + TEXT(".uasset"))))
                {
                    Header = Stem + TEXT(".uasset");
                    Body = Stem + TEXT(".uexp");
                    break;
                }
            }
        }
        if (Header.IsEmpty())
        {
            Reads[Package];
            return false;
        }
        auto HeaderBytes = std::make_shared<Bytes>(), BodyBytes = std::make_shared<Bytes>();
        if (!ReadBytes(Header, 64 * 1024 * 1024, *HeaderBytes) || !ReadBytes(Body, 64 * 1024 * 1024, *BodyBytes))
        {
            Reads[Package];
            return false;
        }
        Out.Header = HeaderBytes;
        Out.Body = BodyBytes;
        Reads[Package] = ReadDigest(*HeaderBytes, *BodyBytes);
        return true;
    }

    const M::MaterialMasterFamily& Family(const std::string& Master)
    {
        const std::string Key = P4MapKit::PropertyIdentity(Master);
        auto Found = Families.find(Key);
        if (Found == Families.end())
        {
            auto& Memos = SharedArchive->FamilyMemos[Key];
            for (const auto& Memo : Memos)
                if (Memo.Instances == Instances && ReadsSame(Memo.Reads))
                {
                    Found = Families.emplace(Key, Memo.Family).first;
                    break;
                }
            if (Found == Families.end())
            {
                ++SharedArchive->FamilyBuilds;
                Found = Families
                            .emplace(Key,
                                M::RecoverMaterialMasterInGameForm(Master, Instances, Sources, SharedArchive->Access))
                            .first;
                if (Found->second.HasGraph)
                    TypeEngineTextureSamplers(Found->second.Merge.Graph);
                // Everything read so far: a superset of what the recovery used, so a match is exact.
                if (Memos.size() < 8)
                    Memos.push_back({Instances, Reads, Found->second});
            }
        }
        return Found->second;
    }
};

bool P4CurrentRecoveredMaterialParentRecipe(
    const FString& Package, const FString& ParentSourceSha256, const FString& ExpectedRecipeMd5, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || ExpectedRecipeMd5.Len() != 32 || ParentSourceSha256.Len() != 64)
    {
        Error = TEXT("MIC parent recipe/source identity is incomplete");
        return false;
    }
    const auto Canonical = Utf8(Package), Key = P4MapKit::PropertyIdentity(Canonical);
    for (const auto* State : FP4MaterialRecovery::FState::Live())
    {
        if (!State->SharedArchive->Archive || !State->SharedArchive->ArchiveBytes)
            continue;
        const auto Found = State->SharedArchive->FamilyMemos.find(Key);
        if (Found == State->SharedArchive->FamilyMemos.end())
            continue;
        for (const auto& Memo : Found->second)
        {
            if (!Memo.Family.HasGraph || Memo.Family.Master != Canonical || !Memo.Reads.count(Canonical) ||
                Memo.Reads.at(Canonical).empty() ||
                !P4ExactNativeString(FMD5::HashAnsiString(*Wide(Memo.Family.Merge.Graph.Dump())), ExpectedRecipeMd5))
                continue;
            bool Current = true;
            for (const auto& Read : Memo.Reads)
            {
                const FString Name = Wide(Read.first);
                FP4KadanSourceBinding Source;
                FString Why;
                if (Read.second.empty())
                {
                    if (P4KadanHasBoundSource(Name))
                    {
                        Current = false;
                        break;
                    }
                    continue;
                }
                if (!P4KadanReadBoundSource(Name, Source, Why) || !P4KadanSourceBindingCurrent(Source, Why) ||
                    (Read.first == Canonical && !P4ExactNativeString(Source.SourceDigest, ParentSourceSha256)) ||
                    FP4MaterialRecovery::FState::ReadDigest(
                        Bytes(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num()),
                        Bytes(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num())) != Read.second)
                {
                    Current = false;
                    break;
                }
            }
            if (Current)
                return true;
        }
    }
    Error = TEXT("MIC parent has no existing recovered recipe with matching MD5 and current raw-source read witness");
    return false;
}

FP4MaterialRecovery::FP4MaterialRecovery(
    const FString& GameFolder, const TArray<TSharedPtr<FP4KadanSourceAsset>>& Assets, const FString& ShaderArchive)
    : State(MakeUnique<FState>())
{
    State->GameFolder = GameFolder;
    State->ShaderArchiveFile = ShaderArchive;
    TSet<FString> Seen;
    for (const auto& Asset : Assets)
    {
        if (!Asset.IsValid())
            continue;
        State->Files.Add(Asset->Package.ToLower(), TPair<FString, FString>(Asset->Header, Asset->Body));
        State->Rows.Add(Asset->Package.ToLower(), Asset);
        if (Asset->Kind == TEXT("MaterialInstanceConstant") && !Seen.Contains(Asset->Package))
        {
            Seen.Add(Asset->Package);
            State->Instances.push_back(Utf8(Asset->Package));
        }
    }
    FState* Raw = State.Get();
    State->Sources.Raw = [Raw](const std::string& Package, M::MaterialOriginalImportRawAsset& Out)
    { return Raw->RawAsset(Package, Out); };
    // The used-resource trace that runs before a family recovers each of its owners already recovered
    // their graphs. Reuse one only when its every read gives the same bytes here, under the same archive.
    State->Sources.KnownOwnerGraph = [Raw](const M::MaterialSourceAsset& Asset,
                                         const std::vector<M::MaterialSourceAsset>& Collections, M::Json& Graph)
    {
        const FString& Archive = Raw->SharedArchive->ArchiveSha256;
        if (Archive.Len() != 64 || !IsInGameThread())
            return false;
        std::vector<std::string> Packages;
        for (const auto& Collection : Collections)
            Packages.push_back(Collection.Package.Package);
        return P4FindTracedMaterialOwnerGraph(Asset.Package.Package, Raw->Sources.Raw, Archive, Packages, Graph);
    };
}

FP4MaterialRecovery::FP4MaterialRecovery(
    const FP4MaterialRecovery& ArchiveOwner, const TArray<TSharedPtr<FP4KadanSourceAsset>>& Assets)
    : FP4MaterialRecovery(ArchiveOwner.State->GameFolder, Assets, ArchiveOwner.State->ShaderArchiveFile)
{
    State->SharedArchive = ArchiveOwner.State->SharedArchive;
}

FP4MaterialRecovery::~FP4MaterialRecovery() = default;

FP4MaterialUsedResources FP4MaterialRecovery::DiscoverUsedResources(
    const FP4MaterialFamilySources& Family, const TFunction<bool(FString&)>& SourceCurrent)
{
    FString CurrentError;
    if (!SourceCurrent || !SourceCurrent(CurrentError))
    {
        FP4MaterialUsedResources Result;
        Result.SourceGuardChecks = SourceCurrent ? 1 : 0;
        Result.Reason = CurrentError.IsEmpty() ? TEXT("Current material source guard is required") : CurrentError;
        Result.RefusalCategory = TEXT("material used native current");
        return Result;
    }
    if (!State->OpenArchive())
    {
        FP4MaterialUsedResources Result;
        Result.SourceGuardChecks = 1;
        Result.Status = EP4MaterialUsedResourceStatus::Incomplete;
        Result.Reason = State->SharedArchive->ArchiveProblem;
        Result.RefusalCategory = TEXT("material used native archive");
        return Result;
    }
    if (State->SharedArchive->ArchiveSha256.IsEmpty())
    {
        State->SharedArchive->ArchiveSha256 = Wide(
            P4MapKit::CustomMaterial::Hex(P4MapKit::CustomMaterial::Sha256(*State->SharedArchive->ArchiveBytes)));
        ++State->SharedArchive->ArchiveDigestComputeCount;
    }
    auto Result = P4DiscoverMaterialUsedResourcesNative(
        Family, State->Sources.Raw, State->SharedArchive->Access, State->SharedArchive->ArchiveSha256, SourceCurrent);
    ++Result.SourceGuardChecks;
    return Result;
}
int32 FP4MaterialRecovery::ArchiveDigestComputes() const
{
    return State->SharedArchive->ArchiveDigestComputeCount;
}

FP4MaterialRecoveryResult FP4MaterialRecovery::Recover(const FString& Package)
{
    FString Kind;
    try
    {
        M::MaterialSourceAsset Asset;
        const auto Resolve = State->Sources.Resolver();
        if (!Resolve(M::MaterialSourcePackage(Utf8(Package)), Asset))
            return Refuse(
                Kind, Package + TEXT(" is not among the game files."), TEXT("a material whose package is missing"));
        const auto Chain = M::ReadOriginalMaterialParentChain(Asset, Resolve);
        Kind = Wide(Chain.back().Class);
        if (!State->OpenArchive())
            return Refuse(Kind, State->SharedArchive->ArchiveProblem, TEXT("the game's shader library is missing"));
        const auto& Family = State->Family(Chain.front().Package);
        FP4MaterialRecoveryResult Result;
        Result.Kind = Kind;
        for (const auto& Member : Family.Members)
            Result.FamilyMembers.Add(Wide(Member));
        for (const auto& Owner : Family.Owners)
            Result.FamilyOwners.Add(Wide(Owner));
        for (const auto& User : Family.Users)
            Result.FamilyUsers.Add(Wide(User));
        if (Chain.back().Class == "Material")
        {
            if (!Family.HasGraph)
            {
                const auto Why = Family.Refused.find(Family.Master);
                if (Why != Family.Refused.end())
                    return Refuse(Kind, TEXT("Its graph cannot be recovered from its shaders: ") + Wide(Why->second),
                        Wide(Family.Refusal.at(Family.Master)));
                if (!Family.RefusedMerge.empty())
                    return Refuse(Kind,
                        TEXT("Its permutations do not merge into one graph: ") + Wide(Family.RefusedMerge),
                        Wide(Family.RefusalMerge));
                return Refuse(Kind, TEXT("Its graph cannot be recovered."), TEXT("a master with no graph"));
            }
            Result.Source = Wide(Family.Merge.Graph.Dump());
        }
        else
            Result.Source = Wide(M::MaterialMasterInstance(Family, Asset.Package.Package, State->Sources).Dump());
        for (const auto& Node : Family.Merge.Graph.At("nodes").object)
            Result.bPreviewDiffers |= !M::KadanGameReadMeaning(Node.second.At("type").Text()).empty();
        if (Result.bPreviewDiffers)
            State->PreviewDiffers.AddUnique(Wide(Asset.Package.Package));
        Result.bOk = true;
        return Result;
    }
    catch (const M::Refused& Error)
    {
        return Refuse(Kind, Wide(Error.what()), Wide(Error.name));
    }
    catch (const std::exception& Error)
    {
        return Refuse(Kind, TEXT("An error that is not a refusal yet: ") + Wide(Error.what()),
            TEXT("an error that is not a refusal yet"));
    }
}

FP4MaterialRecoveryResult FP4MaterialRecovery::Import(
    const FString& Package, bool bSaveToDisk, const TFunction<bool(FString&)>& SourceCurrent, bool bWaitForShaders)
{
    FString CurrentError;
    const bool Current = !SourceCurrent || SourceCurrent(CurrentError);
    FP4MaterialRecoveryResult Result = Current
        ? Recover(Package)
        : Refuse(FString(), TEXT("material-source-current-refused: ") + CurrentError,
              TEXT("material source custody changed"));
    if (Result.bOk && SourceCurrent && !SourceCurrent(CurrentError))
        Result = Refuse(Result.Kind, TEXT("material-source-current-refused: ") + CurrentError,
            TEXT("material source custody changed"));
    if (!Result.bOk)
    {
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("valid"), false);
        Report->SetBoolField(TEXT("saved"), false);
        Report->SetStringField(TEXT("error"), Result.Reason);
        Report->SetStringField(TEXT("refusal"), Result.RefusalName);
        FJsonSerializer::Serialize(
            Report, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result.Report));
        return Result;
    }
    if (Result.Kind == TEXT("MaterialInstanceConstant") && P4ImportPublication::IsImporting())
    {
        FP4KadanSourceBinding Binding;
        FString Why;
        // The instance's reads are checked against the files this recovery read them from. A family
        // recovery reads staged family members and metadata that the import's wider scope does not hold.
        TArray<TSharedPtr<FP4KadanSourceAsset>> Own;
        State->Rows.GenerateValueArray(Own);
        FP4KadanImportSourceScope OwnSources(Own);
        const auto Reads = State->Reads;
        const auto CurrentReads = [&, Reads](FString& Error)
        {
            Error.Reset();
            if (!SourceCurrent)
            {
                Error = TEXT("fresh MIC needs the current family lifetime guard");
                return false;
            }
            for (const auto& Read : Reads)
            {
                const FString Name = Wide(Read.first);
                if (Read.second.empty())
                {
                    if (P4KadanHasBoundSource(Name))
                    {
                        Error = TEXT("fresh MIC raw-source absence changed");
                        return false;
                    }
                    continue;
                }
                // Every check of every instance of the family reads the same packages, so the digest of each is
                // made once for the bytes the import keeps.
                FString Digest;
                const auto Make = [](const FP4KadanSourceBinding& Kept)
                {
                    const std::string Made =
                        FState::ReadDigest(Bytes(Kept.Header.GetData(), Kept.Header.GetData() + Kept.Header.Num()),
                            Bytes(Kept.Body.GetData(), Kept.Body.GetData() + Kept.Body.Num()));
                    return BytesToHex(reinterpret_cast<const uint8*>(Made.data()), int32(Made.size()));
                };
                if (!P4KadanBoundSourceDigest(Name, TEXT("material-recovery-read"), Make, Digest, Error) ||
                    Digest != BytesToHex(reinterpret_cast<const uint8*>(Read.second.data()), int32(Read.second.size())))
                {
                    if (Error.IsEmpty())
                        Error = TEXT("fresh MIC recovery/current raw-source bytes differ");
                    return false;
                }
            }
            // The loop reads and compares raw bytes only; it neither constructs
            // UObjects nor yields to editor callbacks. One complete family check
            // after these reads covers this consumption boundary.
            return SourceCurrent(Error);
        };
        const bool OwnRead = Reads.count(Utf8(Package)) && !Reads.at(Utf8(Package)).empty();
        if (!OwnRead || !CurrentReads(Why) || !P4KadanReadBoundSource(Package, Binding, Why))
        {
            if (Why.IsEmpty())
                Why = OwnRead ? TEXT("its game sources could not be read again")
                              : TEXT("its own game source was not read");
            return Refuse(Result.Kind, TEXT("material-instance-native-source-not-qualified: ") + Why,
                TEXT("fresh material instance source custody"));
        }
        // Private authority is minted only here, from this actual Recover's
        // recipe and exact current raw reads. It dies with this synchronous build.
        FP4RecoveredMaterialInstanceProof Proof(Package, Result.Source, MoveTemp(Binding), CurrentReads);
        Result.Report = P4BuildRecoveredMaterialInstanceReport(
            Result.Source, Package, bSaveToDisk, Proof, SourceCurrent);
    }
    else
        Result.Report = Result.Kind == TEXT("Material")
            ? P4BuildMaterialGraphReport(Result.Source, Package, bSaveToDisk, SourceCurrent, bWaitForShaders)
            : P4BuildMaterialInstanceReport(Result.Source, Package, bSaveToDisk, SourceCurrent);
    TSharedPtr<FJsonObject> Report;
    bool Valid = false;
    if (!P4ParseJson(Result.Report, Report) || !Report->TryGetBoolField(TEXT("valid"), Valid) || !Valid)
    {
        Result.bOk = false;
        if (!Report.IsValid() || !Report->TryGetStringField(TEXT("error"), Result.Reason) || Result.Reason.IsEmpty())
            Result.Reason = TEXT("The recovered material could not be built.");
        Result.RefusalName = TEXT("a recovered material the editor does not build");
    }
    return Result;
}

int32 FP4MaterialRecovery::CacheLandscapeColorPreviews(
    UWorld* World, const FP4KadanSourceAsset& Source, const FP4KadanImportSettings& Settings, TArray<FString>& Refusals)
{
    if (!World || !World->PersistentLevel)
        return 0;
    TMap<FString, FString> Owners;
    for (AActor* Actor : World->PersistentLevel->Actors)
        if (IsValid(Actor))
        {
            TInlineComponentArray<ULandscapeComponent*> Parts;
            Actor->GetComponents(Parts);
            for (auto* Part : Parts)
                if (Part->GetWeightmapTextures().Num() && Part->GetWeightmapTextures()[0])
                    for (auto* Slot : Part->MaterialInstances)
                    {
                        int32 Depth = 0;
                        for (UMaterialInstanceConstant* Instance = Slot;
                            Instance && Instance->IsIn(World) && ++Depth <= 64;
                            Instance = Cast<UMaterialInstanceConstant>(Instance->Parent))
                            if (auto* Landscape = Cast<ULandscapeMaterialInstanceConstant>(Instance))
                                if (Landscape->GetStaticParameters().TerrainLayerWeightParameters.Num())
                                    Owners.FindOrAdd(
                                        Landscape->GetPathName()) = Part->GetWeightmapTextures()[0]->GetPathName();
                    }
        }
    if (!Owners.Num())
        return 0;
    FP4OriginalWorldSourcePtr Original;
    FString Error;
    if (!P4ReadOriginalWorldSource(Source.Header, Source.Body, Source.Package, Original, Error) ||
        !State->OpenArchive())
    {
        Refusals.Add(Error.IsEmpty() ? State->SharedArchive->ArchiveProblem : Error);
        return 0;
    }
    const Bytes Header(Original->Header().GetData(), Original->Header().GetData() + Original->Header().Num());
    const Bytes Body(Original->Body().GetData(), Original->Body().GetData() + Original->Body().Num());
    const auto Package = P4MapKit::ReadPackage(Header, Body);
    TArray<FP4KadanSourceBinding> Bindings;
    FString Cache;
    FP4KadanContainerIndex CacheIndex;
    bool CheckedCache = false, CacheReady = false, UsedCache = false;
    const M::MaterialSourceAssetResolver Resolve = [&](const std::string& Name, M::MaterialSourceAsset& Asset)
    {
        FP4KadanSourceBinding Binding;
        const FString NameText = Wide(Name);
        if (P4KadanHasBoundSource(NameText))
        {
            if (!P4KadanReadBoundSource(NameText, Binding, Error))
                return false;
        }
        else
        {
            // Resume and the import library can retain an external material without
            // executing it. Its raw parents still come from the current selected
            // conversion cache, resolved by that game's index, never a guessed or
            // alternate tree. A parent this project never converted is converted now.
            if (!CheckedCache)
            {
                CheckedCache = true;
                CacheReady = P4KadanRetainedConversionCacheCurrent(Settings, Cache, Error) &&
                    P4KadanReadContainerIndex(Settings, CacheIndex, Error);
            }
            FString Mounted;
            if (!CacheReady || !CacheIndex.Resolve(NameText, Mounted, Error))
                return false;
            auto Row = MakeShared<FP4KadanSourceAsset>();
            Row->Header = Cache / Mounted;
            FString Unready;
            if (!P4KadanExtractedPackageReady(Row->Header, NameText, Unready) &&
                !P4KadanConvertMountedOriginal(Settings, Mounted, Cache, Error))
                return false;
            Row->Body = FPaths::ChangeExtension(Row->Header, TEXT("uexp"));
            TArray<uint8> H, B;
            if (!P4ReadBoundedSourceFile(Row->Header, 64 * 1024 * 1024, H, Error) ||
                !P4ReadBoundedSourceFile(Row->Body, 64 * 1024 * 1024, B, Error))
                return false;
            const auto Inventory = P4MapKit::ReadPackage(
                Bytes(H.GetData(), H.GetData() + H.Num()), Bytes(B.GetData(), B.GetData() + B.Num()));
            if (!Inventory.Error.Code.empty() || FName(*Wide(Inventory.Package)) != FName(*NameText))
                return false;
            Row->Package = Wide(Inventory.Package);
            FP4KadanImportSourceScope Scoped({Row});
            if (!P4KadanReadBoundSource(Row->Package, Binding, Error))
                return false;
            UsedCache = true;
        }
        const auto H = std::make_shared<const Bytes>(
            Binding.Header.GetData(), Binding.Header.GetData() + Binding.Header.Num());
        const auto B = std::make_shared<const Bytes>(
            Binding.Body.GetData(), Binding.Body.GetData() + Binding.Body.Num());
        auto Inventory = P4MapKit::ReadPackage(*H, *B);
        if (!Inventory.Error.Code.empty())
            return false;
        Bindings.Add(MoveTemp(Binding));
        Asset = {MoveTemp(Inventory), B};
        return true;
    };
    const auto& Held = State->SharedArchive->Access;
    M::LandscapePreviewArchiveAccess Access{Held.MapIndices, Held.Payload, Held.Frequency};
    TArray<TSharedPtr<FJsonValue>> Recipes;
    const std::string WorldPrefix = Package.Package.substr(Package.Package.find_last_of('/') + 1) + ".PersistentLevel.";
    for (std::size_t Index = 0; Index < Package.Exports.size(); ++Index)
    {
        std::string Object;
        if (!Package.Reference(std::int32_t(Index + 1), Object))
            continue;
        if (Object.compare(0, WorldPrefix.size(), WorldPrefix) != 0)
            continue;
        const FString Canonical = Wide(M::LandscapePreviewObjectPath(Package, Object));
        const FString* Weightmap = Owners.Find(Canonical);
        if (!Weightmap)
            continue;
        try
        {
            const auto Recovered = M::RecoverInlineLandscapeColorPreview(
                Package, Body, Index, Resolve, Access, Utf8(*Weightmap));
            auto Recipe = MakeShared<FJsonObject>();
            Recipe->SetStringField(TEXT("object"), Wide(Recovered.Object));
            Recipe->SetStringField(TEXT("parent"), Wide(Recovered.Parent));
            Recipe->SetStringField(TEXT("shader_type"), FString::Printf(TEXT("%016llx"), Recovered.ShaderType));
            Recipe->SetStringField(TEXT("graph"), Wide(Recovered.Graph.Dump()));
            Recipes.Add(MakeShared<FJsonValueObject>(Recipe));
        }
        catch (const M::Refused& Why)
        {
            Refusals.Add(
                Wide(Object) + TEXT(": ") + Wide(Why.what()) + (Error.IsEmpty() ? FString() : TEXT("; ") + Error));
        }
        catch (const std::exception& Why)
        {
            Refusals.Add(Wide(Object) + TEXT(": ") + Wide(Why.what()));
        }
    }
    if (!Recipes.Num() && !Refusals.Num())
        Refusals.Add(TEXT("The original inline Landscape shader owners did not match their source exports"));
    if (!Original->Current(Error))
    {
        Refusals.Add(Error);
        return 0;
    }
    if (UsedCache)
    {
        FString Current;
        if (!P4KadanRetainedConversionCacheCurrent(Settings, Current, Error) || !FPaths::IsSamePath(Cache, Current))
        {
            Refusals.Add(Error);
            return 0;
        }
    }
    for (const auto& Binding : Bindings)
        if (!P4KadanSourceBindingCurrent(Binding, Error))
        {
            Refusals.Add(Error);
            return 0;
        }
    if (!P4CacheOriginalLandscapeColorPreviews(World, Original->SourceDigest(), Recipes, Error))
    {
        Refusals.Add(Error);
        return 0;
    }
    return Recipes.Num();
}

void FP4MaterialRecovery::ReportLandscapeColorPreviews(const FString& AssetPath, const FP4KadanSourceAsset& Source,
    const FP4KadanImportSettings& Settings, const TSharedPtr<FJsonObject>& Reply, TArray<FString>& Warnings)
{
    UWorld* World = FindObject<UWorld>(nullptr, *AssetPath);
    TArray<FString> Refusals;
    const int32 Owners = CacheLandscapeColorPreviews(World, Source, Settings, Refusals);
    Reply->SetNumberField(TEXT("landscape_color_preview_owners"), Owners);
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const FString& Refusal : Refusals)
    {
        Values.Add(MakeShared<FJsonValueString>(Refusal));
        Warnings.AddUnique(Source.Package + TEXT(" landscape color preview: ") + Refusal);
    }
    Reply->SetArrayField(TEXT("landscape_color_preview_refusals"), Values);
}

int32 FP4MaterialRecovery::MastersWithGraph() const
{
    int32 Count = 0;
    for (const auto& Item : State->Families)
        Count += Item.second.HasGraph ? 1 : 0;
    return Count;
}
int32 FP4MaterialRecovery::ShaderArchiveReadAttempts() const
{
    return State->SharedArchive->ReadAttempts;
}
int32 FP4MaterialRecovery::FamilyRecoveryBuilds() const
{
    return State->SharedArchive->FamilyBuilds;
}
int32 FP4MaterialRecovery::OwnerGraphReuses() const
{
    return P4TracedMaterialOwnerGraphHits();
}

FString FP4MaterialRecovery::PreviewNote() const
{
    const TArray<FString>& Names = State->PreviewDiffers;
    if (!Names.Num())
        return FString();
    FString List;
    for (int32 I = 0; I < Names.Num(); ++I)
        List += (I == 0 ? TEXT("")
                        : I + 1 == Names.Num() ? TEXT(" and ")
                                               : TEXT(", ")) +
            FPackageName::GetLongPackageAssetName(Names[I]);
    return FString::Printf(
        TEXT("The editor draws %d %s a little differently from the game, and they look right in the game. %s %s."),
        Names.Num(), Names.Num() == 1 ? TEXT("material") : TEXT("materials"),
        Names.Num() == 1 ? TEXT("It is") : TEXT("They are"), *List);
}

int32 FP4MaterialRecovery::MastersWithoutGraph() const
{
    return int32(State->Families.size()) - MastersWithGraph();
}
