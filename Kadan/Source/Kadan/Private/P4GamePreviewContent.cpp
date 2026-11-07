#include "P4GamePreviewContent.h"
#include "P4GameBuild.h"
#include "P4Common.h"
#include "P4KadanSettings.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4GamePreviewWorld.h"
#include "P4GamePreviewBootstrap.h"
#include "P4GamePreviewTemplate.h"
#include "P4GamePreviewNavigation.h"
#include "P4GamePreviewStart.h"
#include "P4KadanImport.h"
#include "P4LevelContent.h"
#include "P4AuthoringRows.h"
#include "P4CreatorSource.h"
#include "P4ExportDriver.h"
#include "P4ExportProcess.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Modules/ModuleManager.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
FString PreviewDirectory()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Kadan/Preview"));
}
// Export work stays outside Saved, which Unreal scans for packages.
FString PreviewWorkDirectory()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / TEXT("Kadan/Preview"));
}

struct FPackageDigest
{
    FString Stamp, Digest;
};
struct FPreviewContentState
{
    FDelegateHandle Modified;
    TMap<FName, uint64> Revisions;
    TMap<FName, FPackageDigest> Digests;
    TSet<FString> RuntimeRoots;
    FP4GamePreviewContentResult Cached;
    FString PreviousId;
    bool Staging = false;
    FPreviewContentState()
    {
        Modified = FCoreUObjectDelegates::OnObjectModified.AddRaw(this, &FPreviewContentState::Changed);
    }
    ~FPreviewContentState() { FCoreUObjectDelegates::OnObjectModified.Remove(Modified); }
    void Changed(UObject* Object)
    {
        if (!Staging && IsValid(Object))
        {
            const FName Package = Object->GetOutermost()->GetFName();
            if (P4IsContentPackageIgnoringCase(Package.ToString()) || Package.ToString().StartsWith(TEXT("/Temp/")) ||
                Package.ToString().StartsWith(TEXT("/Engine/")))
                ++Revisions.FindOrAdd(Package);
        }
    }
};
FPreviewContentState& State()
{
    static FPreviewContentState Value;
    return Value;
}

bool IsRuntimePackage(const FString& Package)
{
    // Also recognize our exact generated identity after an interrupted
    // editor session left an ephemeral registry-cache row behind.
    const FString Prefix = TEXT(P4_PREVIEW_PACKAGE_ROOT);
    if (Package.StartsWith(Prefix, ESearchCase::CaseSensitive))
    {
        FString Identity = Package.Mid(Prefix.Len());
        int32 Slash = INDEX_NONE;
        if (Identity.FindChar(TCHAR('/'), Slash))
            Identity.LeftInline(Slash);
        if (Identity.Len() == 49 && Identity[32] == TCHAR('_'))
        {
            bool Hex = true;
            for (int32 At = 0; At < Identity.Len(); ++At)
                if (At != 32 && !FChar::IsHexDigit(Identity[At]))
                {
                    Hex = false;
                    break;
                }
            if (Hex)
                return true;
        }
    }
    for (const FString& Root : State().RuntimeRoots)
        if (Package == Root || Package.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase))
            return true;
    return false;
}

// FObjectWriter reads current native source/bulk data as well as properties.
// DuplicateVerbatim prevents its normal localization namespace creation on
// the authored package. Cache by file identity and native editor Modify calls.
bool PackageDigest(UPackage* Package, FString& Digest, FString& Error, const FString& WorldSourceObjectPath)
{
    if (!Package)
    {
        Error = TEXT("A preview source package disappeared during inspection.");
        return false;
    }
    FString File;
    FPackageName::DoesPackageExist(Package->GetName(), nullptr, &File);
    const FFileStatData Stat = IFileManager::Get().GetStatData(*File);
    FPreviewContentState& Cache = State();
    const FString Stamp = FString::Printf(TEXT("%s|%lld|%lld|%llu|%d"), *File, Stat.FileSize,
        Stat.ModificationTime.GetTicks(), Cache.Revisions.FindRef(Package->GetFName()), Package->IsDirty());
    if (const FPackageDigest* Previous = Cache.Digests.Find(Package->GetFName()))
        if (Previous->Stamp == Stamp)
        {
            Digest = Previous->Digest;
            return true;
        }
    FString Evidence = TEXT("kadan-preview-package/1\n") + Package->GetName() + TEXT("\n");
    if (Stat.bIsValid && Stat.FileSize > 0)
    {
        int64 Size = 0;
        FString DiskDigest;
        if (!P4HashFile(File, int64(1024) * 1024 * 1024, Size, DiskDigest))
        {
            Error = TEXT("Cannot fingerprint the creator package ") + Package->GetName();
            return false;
        }
        Evidence += DiskDigest + TEXT("\n");
    }
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    Objects.Sort([](const UObject& A, const UObject& B) { return A.GetPathName() < B.GetPathName(); });
    for (UObject* Object : Objects)
    {
        if ((Object->HasAnyFlags(RF_Transient) && Object->GetPathName() != WorldSourceObjectPath) ||
            Object->IsA<UMetaData>())
            continue;
        TArray<uint8> Bytes;
        FObjectWriter Writer(Object, Bytes, true, true, false, PPF_DuplicateVerbatim);
        FString Part;
        const bool Hashed = P4HashByteStream(
            [&](const TFunction<bool(const uint8*, int32)>& Feed)
            {
                for (int32 At = 0; At < Bytes.Num();)
                {
                    const int32 Count = FMath::Min(65536, Bytes.Num() - At);
                    if (!Feed(Bytes.GetData() + At, Count))
                        return false;
                    At += Count;
                }
                return true;
            },
            Part);
        if (Writer.IsError() || !Hashed)
        {
            Error = TEXT("Cannot fingerprint the current creator content ") + Object->GetPathName();
            return false;
        }
        Evidence += Object->GetPathName() + TEXT(" ") + Part + TEXT("\n");
    }
    if (!P4HashUtf8(Evidence, Digest))
    {
        Error = TEXT("Cannot fingerprint creator content.");
        return false;
    }
    Cache.Digests.Add(Package->GetFName(), {Stamp, Digest});
    return true;
}

class FCloneReferences : public FArchiveReplaceObjectRef<UObject>
{
    using Super = FArchiveReplaceObjectRef<UObject>;
    const TMap<FString, FString>& Packages;

public:
    FCloneReferences(UObject* Object, const TMap<UObject*, UObject*>& Objects, const TMap<FString, FString>& InPackages)
        : Super(Object, Objects, false, true, false, true, false), Packages(InPackages)
    {
        SerializeSearchObject();
    }
    using Super::operator<<;
    virtual FArchive& operator<<(FSoftObjectPath& Path) override
    {
        if (const FString* Target = Packages.Find(Path.GetLongPackageName()))
        {
            const FString Old = Path.ToString();
            Path = FSoftObjectPath(*Target + Old.Mid(Path.GetLongPackageName().Len()));
        }
        return *this;
    }
    virtual FArchive& operator<<(FSoftObjectPtr& Pointer) override
    {
        FSoftObjectPath Path = Pointer.ToSoftObjectPath();
        *this << Path;
        Pointer = FSoftObjectPtr(Path);
        return *this;
    }
};

struct FCloneScope
{
    TArray<TStrongObjectPtr<UPackage>> Sources, Packages;
    TArray<TStrongObjectPtr<UObject>> Objects;
    TArray<TWeakObjectPtr<UObject>> Registered;
    TMap<UPackage*, bool> Dirty;
    TMap<FString, FString> PackagePaths;
    TMap<UObject*, UObject*> Replacements;
    const FString& Failure;
    FString Phase = TEXT("cloning source packages");
    explicit FCloneScope(const FString& Error) : Failure(Error) {}
    ~FCloneScope()
    {
        // Native replacement deliberately destroys the old settings actor.
        // GC may then null its FGCObject-backed strong pointer while a
        // conversion/import pumps the editor. Cleanup must preserve the
        // original failure rather than asserting through operator->.
        if (!Failure.IsEmpty())
            UE_LOG(LogTemp, Warning, TEXT("Kadan preview preparation failed while %s: %s"), *Phase, *Failure);
        for (const auto& Asset : Registered)
            if (UObject* Object = Asset.Get())
                FAssetRegistryModule::AssetDeleted(Object);
        TSet<UPackage*> Owned;
        for (const auto& Held : Packages)
            if (UPackage* Package = Held.Get())
                Owned.Add(Package);
        TSet<UObject*> Released;
        for (const auto& Held : Objects)
        {
            UObject* Object = Held.Get();
            if (!IsValid(Object) || Released.Contains(Object))
                continue;
            if (!Owned.Contains(Object->GetOutermost()))
            {
                UE_LOG(LogTemp, Warning, TEXT("Kadan preview cleanup refused an object outside its owned packages: %s"),
                    *Object->GetPathName());
                continue;
            }
            Released.Add(Object);
            Object->ClearFlags(RF_Public | RF_Standalone);
            Object->SetFlags(RF_Transient);
            Object->MarkPendingKill();
        }
        Objects.Reset();
        for (const auto& Held : Packages)
        {
            UPackage* Package = Held.Get();
            if (!IsValid(Package))
                continue;
            Package->SetDirtyFlag(false);
            Package->Rename(*(TEXT("/Temp/KadanPreviewReleased/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
            Package->MarkPendingKill();
        }
        for (const auto& Held : Sources)
            if (UPackage* Package = Held.Get())
                if (IsValid(Package))
                    if (const bool* WasDirty = Dirty.Find(Package))
                        Package->SetDirtyFlag(*WasDirty);
    }
    // Export reports the private copies by their generated names. The creator knows their own.
    FString SourceNames(FString Text) const
    {
        TArray<TPair<FString, FString>> Pairs;
        for (const auto& Pair : PackagePaths)
            Pairs.Emplace(Pair.Value, Pair.Key);
        Pairs.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B)
            { return A.Key.Len() > B.Key.Len(); });
        for (const auto& Pair : Pairs)
            Text.ReplaceInline(*Pair.Key, *Pair.Value, ESearchCase::CaseSensitive);
        return Text;
    }
    // GeometryName is the runtime name of a new level's geometry, empty for a game map.
    bool Prepare(
        const FP4GamePreviewContentPlan& Plan, const FString& Root, const FString& GeometryName, FString& Error)
    {
        UWorld* SourceWorld = Plan.WorldSourceObjectPath.IsEmpty()
            ? nullptr
            : FindObject<UWorld>(nullptr, *Plan.WorldSourceObjectPath);
        if (!Plan.WorldSourceObjectPath.IsEmpty() && !SourceWorld)
        {
            Error = TEXT("The scratch editor world disappeared before preparation.");
            return false;
        }
        for (const FString& Name : Plan.SourcePackages)
        {
            UPackage* Source = FindPackage(nullptr, *Name);
            if (!Source)
                Source = LoadPackage(nullptr, *Name, LOAD_NoWarn | LOAD_Quiet);
            if (!Source)
            {
                Error = TEXT("The creator package disappeared: ") + Name;
                return false;
            }
            Sources.Emplace(Source);
            Dirty.Add(Source, Source->IsDirty());
            Source->FullyLoad();
            const bool IsWorld = SourceWorld && SourceWorld->GetOutermost() == Source;
            FString Relative;
            if (IsWorld)
                Relative = GeometryName;
            else if (P4IsContentPackageIgnoringCase(Name))
                Relative = P4ContentRelativePath(Name);
            else if (Name.StartsWith(TEXT("/Engine/")))
                Relative = TEXT("Engine/") + Name.Mid(8);
            else
            {
                Error = TEXT("Unsupported preview source package: ") + Name;
                return false;
            }
            const FString Target = Root / Relative;
            if (!FPackageName::IsValidLongPackageName(Target) || FindPackage(nullptr, *Target) ||
                FPackageName::DoesPackageExist(Target))
            {
                Error = TEXT("The private preview destination is occupied: ") + Target;
                return false;
            }
            UPackage* Package = CreatePackage(*Target);
            if (IsWorld)
                Package->SetPackageFlags(PKG_ContainsMap);
            Packages.Emplace(Package);
            PackagePaths.Add(Name, Target);
            Replacements.Add(Source, Package);
        }
        // UE's asset duplication performs the Blueprint-generated-class work.
        // Duplicate all other top-level exports too, retaining their names and
        // collecting every source->clone pair before cross-package remapping.
        for (const auto& Held : Sources)
        {
            UPackage* Source = Held.Get();
            UPackage* Target = CastChecked<UPackage>(Replacements.FindChecked(Source));
            TArray<UObject*> Top;
            GetObjectsWithOuter(Source, Top, false);
            Top.Sort(
                [](const UObject& A, const UObject& B)
                {
                    if (A.IsA<UBlueprint>() != B.IsA<UBlueprint>())
                        return A.IsA<UBlueprint>();
                    if (A.IsAsset() != B.IsAsset())
                        return A.IsAsset();
                    return A.GetName() < B.GetName();
                });
            for (UObject* Object : Top)
            {
                const bool IsWorld = Object == SourceWorld;
                if (Object->IsA<UMetaData>() || (Object->HasAnyFlags(RF_Transient) && !IsWorld))
                    continue;
                const FString CopyName = IsWorld ? GeometryName : Object->GetName();
                UObject* Copy = FindObject<UObject>(Target, *CopyName);
                if (!Copy)
                {
                    if (Object->IsA<UClass>() || Object->HasAnyFlags(RF_ClassDefaultObject))
                    {
                        Error =
                            TEXT(
                                "Native Blueprint duplication did not create its generated class or default object: ") +
                            Object->GetPathName();
                        return false;
                    }
                    FObjectDuplicationParameters Parameters(Object, Target);
                    Parameters.DestName = FName(*CopyName);
                    if (IsWorld)
                        Parameters.DuplicateMode = EDuplicateMode::World;
                    Parameters.FlagMask = RF_AllFlags & ~RF_Transient;
                    // A package seed makes the writer recursively duplicate
                    // every referenced sibling in that package. Blueprint
                    // classes/CDOs must instead be regenerated by native
                    // PostDuplicateBlueprint, as in the editor's Duplicate
                    // action. Keep package mappings for the final remap,
                    // while seeding only already-created object copies.
                    for (const auto& Pair : Replacements)
                        if (!Pair.Key->IsA<UPackage>())
                            Parameters.DuplicationSeed.Add(Pair.Key, Pair.Value);
                    TMap<UObject*, UObject*> Created;
                    Parameters.CreatedObjects = &Created;
                    Copy = StaticDuplicateObjectEx(Parameters);
                    if (!Copy)
                    {
                        Error = TEXT("Cannot make a preview copy of ") + Object->GetPathName();
                        return false;
                    }
                    for (const auto& Pair : Created)
                    {
                        // Preserve exclusions for preview actors, streaming
                        // levels and editor helpers. Only the unsaved root
                        // world itself becomes a cookable private object.
                        if (Pair.Key != SourceWorld && Pair.Key->HasAnyFlags(RF_Transient))
                            Pair.Value->SetFlags(RF_Transient);
                        Replacements.Add(Pair.Key, Pair.Value);
                        Objects.Emplace(Pair.Value);
                    }
                }
                Replacements.Add(Object, Copy);
            }
            TArray<UObject*> OriginalObjects;
            GetObjectsWithOuter(Source, OriginalObjects, true);
            for (UObject* Object : OriginalObjects)
            {
                FString Path = Target->GetName() + Object->GetPathName().Mid(Source->GetName().Len());
                if (SourceWorld && SourceWorld->GetOutermost() == Source && Object->IsIn(SourceWorld))
                    Path = Target->GetName() + TEXT(".") + GeometryName +
                        Object->GetPathName().Mid(SourceWorld->GetPathName().Len());
                if (UObject* Copy = FindObject<UObject>(nullptr, *Path))
                    Replacements.Add(Object, Copy);
            }
            TArray<UObject*> CopiedObjects;
            GetObjectsWithOuter(Target, CopiedObjects, true);
            for (UObject* Copy : CopiedObjects)
                Objects.Emplace(Copy);
        }
        for (const auto& Held : Packages)
        {
            TArray<UObject*> All;
            GetObjectsWithOuter(Held.Get(), All, true);
            for (UObject* Object : All)
                FCloneReferences Remap(Object, Replacements, PackagePaths);
            Held->SetDirtyFlag(false);
            for (UObject* Object : All)
                if (Object->IsAsset() && Object->GetOuter() == Held.Get())
                {
                    FAssetRegistryModule::AssetCreated(Object);
                    Registered.Emplace(Object);
                }
        }
        return true;
    }
    bool NormalizeEngineMaterialSamples(UMaterial* Graph, FString& Error)
    {
        if (!Graph || !P4IsTrustedGamePreviewEngineCopy(Graph))
        {
            Error = TEXT("Texture sample normalization requires a proven private engine material copy.");
            return false;
        }
        if (Graph->Expressions.Num() > 4096)
        {
            Error = TEXT("The private engine material exceeds the preview expression limit.");
            return false;
        }
        TMap<UTexture*, int32> Counts;
        TSet<FName> Names;
        for (UMaterialExpression* Expression : Graph->Expressions)
        {
            if (!Expression)
                continue;
            if (Expression->HasAParameterName())
                Names.Add(Expression->GetParameterName());
            if (Expression->GetClass() == UMaterialExpressionTextureSample::StaticClass())
                if (UTexture* Texture = CastChecked<UMaterialExpressionTextureSample>(Expression)->Texture)
                    ++Counts.FindOrAdd(Texture);
        }
        // A named texture binding can be sampled at several UVs. Normalize
        // only repeated plain bindings on disposable engine copies; the
        // ordinary creator material contract remains unchanged.
        TMap<UTexture*, FName> Parameters;
        TMap<UObject*, UObject*> Swaps;
        int32 NextName = 0;
        for (UMaterialExpression* Expression : Graph->Expressions)
        {
            if (!Expression || Expression->GetClass() != UMaterialExpressionTextureSample::StaticClass())
                continue;
            UMaterialExpressionTextureSample* Sample = CastChecked<UMaterialExpressionTextureSample>(Expression);
            if (Counts.FindRef(Sample->Texture) < 2)
                continue;
            if (!Sample->Texture->IsA<UTexture2D>() || Sample->TextureObject.Expression ||
                Sample->AutomaticViewMipBiasValue.Expression)
            {
                Error =
                    TEXT(
                        "A repeated engine texture sample cannot be normalized without changing its texture-object or view-mip override: ") +
                    Sample->GetPathName();
                return false;
            }
            FName* Name = Parameters.Find(Sample->Texture);
            if (!Name)
            {
                FName Candidate;
                do
                {
                    Candidate = FName(*FString::Printf(TEXT("KadanPreviewTexture%d"), NextName++));
                }
                while (Names.Contains(Candidate));
                Names.Add(Candidate);
                Parameters.Add(Sample->Texture, Candidate);
                Name = Parameters.Find(Sample->Texture);
            }
            UMaterialExpressionTextureSampleParameter2D* Parameter =
                NewObject<UMaterialExpressionTextureSampleParameter2D>(Graph, NAME_None, RF_Transactional);
            Objects.Emplace(Parameter);
            // Copy every reflected sample/base field, including all input
            // links, output masks, UVs, mip mode/bias, sampler settings and
            // editor graph links. Only the binding gains a parameter name.
            for (TFieldIterator<FProperty> Field(UMaterialExpressionTextureSample::StaticClass()); Field; ++Field)
                Field->CopyCompleteValue_InContainer(Parameter, Sample);
            Parameter->bIsParameterExpression = true;
            Parameter->ParameterName = *Name;
            Parameter->UpdateParameterGuid(true, false);
            Swaps.Add(Sample, Parameter);
        }
        if (!Swaps.Num())
            return true;
        for (const auto& Package : Packages)
        {
            TArray<UObject*> All;
            GetObjectsWithOuter(Package.Get(), All, true);
            for (UObject* Object : All)
                FArchiveReplaceObjectRef<UObject> Remap(Object, Swaps, false, true, false);
        }
        for (auto& Pair : Replacements)
            if (UObject* const* Replacement = Swaps.Find(Pair.Value))
                Pair.Value = *Replacement;
        for (const auto& Pair : Swaps)
        {
            const FName Name = Pair.Key->GetFName();
            if (!Pair.Key->Rename(nullptr, GetTransientPackage(),
                    REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional) ||
                !Pair.Value->Rename(
                    *Name.ToString(), Graph, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional))
            {
                Error = TEXT("The normalized private texture sample could not retain its object identity.");
                return false;
            }
            Pair.Key->ClearFlags(RF_Public | RF_Standalone);
            Pair.Key->SetFlags(RF_Transient);
        }
        UE_LOG(LogTemp, Display, TEXT("Kadan preview normalized %d repeated texture samples in %s."), Swaps.Num(),
            *Graph->GetPathName());
        return true;
    }
    bool RefreshWorldSettings(UWorld* SourceWorld, UWorld* World, FString& Error)
    {
        AWorldSettings* Source = SourceWorld->PersistentLevel->GetWorldSettings(false);
        AWorldSettings* Target = World->PersistentLevel->GetWorldSettings(false);
        if (!IsValid(Source) || !IsValid(Target))
        {
            Error = TEXT("The prepared world settings disappeared before updating preview references.");
            return false;
        }
        TArray<UObject*> SourceObjects;
        GetObjectsWithOuter(Source, SourceObjects, true);
        SourceObjects.Add(Source);
        TMap<UObject*, UObject*> Changes;
        for (UObject* Object : SourceObjects)
        {
            if (!IsValid(Object))
            {
                Replacements.Remove(Object);
                continue;
            }
            UObject* Previous = Replacements.FindRef(Object);
            const FString Path = Target->GetPathName() + Object->GetPathName().Mid(Source->GetPathName().Len());
            UObject* Current = Object == Source ? Target : FindObject<UObject>(nullptr, *Path);
            if (!IsValid(Current))
            {
                // Editor helpers may be absent from the actual game class.
                // Never publish an alias to the retired settings subtree.
                if (!Object->HasAnyFlags(RF_Transient) && !Object->IsEditorOnly() && IsValid(Object))
                {
                    Error = TEXT("The game world settings could not preserve an authored subobject: ") +
                        Object->GetPathName() + TEXT(" (") + Object->GetClass()->GetPathName() +
                        TEXT("). Expected private object: ") + Path;
                    return false;
                }
                Replacements.Remove(Object);
                continue;
            }
            Replacements.Add(Object, Current);
            Objects.Emplace(Current);
            if (Previous && Previous != Current)
                Changes.Add(Previous, Current);
        }
        for (const auto& Package : Packages)
        {
            UPackage* OwnedPackage = Package.Get();
            if (!IsValid(OwnedPackage))
            {
                Error = TEXT("A private preview package disappeared while refreshing world settings references.");
                return false;
            }
            TArray<UObject*> All;
            GetObjectsWithOuter(OwnedPackage, All, true);
            for (UObject* Object : All)
                if (IsValid(Object))
                    FArchiveReplaceObjectRef<UObject> Remap(Object, Changes, false, true, false);
        }
        return true;
    }
};

bool ResultExists(const FP4GamePreviewContentResult& Result)
{
    if (Result.PakFile.IsEmpty())
        return false;
    for (const FString* File : {&Result.PakFile, &Result.UtocFile, &Result.UcasFile})
        if (IFileManager::Get().FileSize(**File) <= 0)
            return false;
    return Result.ShaderFile.IsEmpty() || IFileManager::Get().FileSize(*Result.ShaderFile) > 0;
}

bool RemoveOwnedTree(const FString& Directory, const FString& Root)
{
    const FString Absolute = FPaths::ConvertRelativePathToFull(Directory);
    if (!FPaths::IsUnderDirectory(Absolute, Root) || Absolute == Root)
        return false;
    if (!IFileManager::Get().DirectoryExists(*Absolute))
        return true;
    TArray<FString> Pending{Absolute};
    for (int32 At = 0; At < Pending.Num(); ++At)
    {
#if PLATFORM_WINDOWS
        const DWORD Attributes = GetFileAttributesW(*Pending[At]);
        if (Attributes == INVALID_FILE_ATTRIBUTES || (Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
#endif
        TArray<FString> Children;
        IFileManager::Get().FindFiles(Children, *(Pending[At] / TEXT("*")), true, true);
        for (const FString& Child : Children)
        {
            const FString Path = Pending[At] / Child;
#if PLATFORM_WINDOWS
            const DWORD AttributesChild = GetFileAttributesW(*Path);
            if (AttributesChild == INVALID_FILE_ATTRIBUTES || (AttributesChild & FILE_ATTRIBUTE_REPARSE_POINT))
                return false;
#endif
            if (IFileManager::Get().DirectoryExists(*Path))
                Pending.Add(Path);
        }
    }
    return IFileManager::Get().DeleteDirectory(*Absolute, false, true);
}

bool PreviewId(const FString& Id)
{
    const FString Prefix = TEXT(P4_PREVIEW_MOD_ID_PREFIX);
    if (!Id.StartsWith(Prefix, ESearchCase::CaseSensitive) || Id.Len() != Prefix.Len() + 32)
        return false;
    for (int32 At = Prefix.Len(); At < Id.Len(); ++At)
        if (!FChar::IsHexDigit(Id[At]))
            return false;
    return true;
}
FString PreviousOutput(const FString& Root)
{
    const FString Marker = Root / TEXT("current.json");
    if (IFileManager::Get().FileSize(*Marker) <= 0 || IFileManager::Get().FileSize(*Marker) > 4096)
        return {};
    FString Text, Id, Error;
    double Format = 0;
    TSharedPtr<FJsonObject> Json;
    if (!FFileHelper::LoadFileToString(Text, *Marker) || !P4ParseJson(Text, Json) || !Json ||
        !Json->TryGetNumberField(TEXT("format"), Format) || Format != 1 || !Json->TryGetStringField(TEXT("id"), Id) ||
        !PreviewId(Id))
        return {};
    FP4ModDescriptor Descriptor;
    if (!P4ReadModDescriptor(Root / TEXT("export-project"), Id, Descriptor, Error) ||
        Descriptor.Name != TEXT("Kadan live preview") || Descriptor.Version != TEXT("0.0.0"))
        return {};
    const FString Manifest = Root / TEXT("output") / Id / TEXT("romfs/tsuchi/mods") / Id / TEXT("mod.json");
    if (IFileManager::Get().FileSize(*Manifest) <= 0 || IFileManager::Get().FileSize(*Manifest) > 4 * 1024 * 1024)
        return {};
    Json.Reset();
    FString ExportId;
    if (!FFileHelper::LoadFileToString(Text, *Manifest) || !P4ParseJson(Text, Json) || !Json ||
        !Json->TryGetStringField(TEXT("id"), ExportId) || ExportId != Id)
        return {};
    return Id;
}
bool RememberOutput(const FString& Root, const FString& Id)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("format"), 1);
    Json->SetStringField(TEXT("id"), Id);
    FString Text;
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
    const FString Marker = Root / TEXT("current.json");
    const FString Temporary = Marker + TEXT(".tmp.") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return false;
    if (!IFileManager::Get().Move(*Marker, *Temporary, true, false, false, false))
    {
        IFileManager::Get().Delete(*Temporary);
        return false;
    }
    return true;
}
} //namespace

bool P4GatherGamePreviewCreatorClosure(UWorld* World, TArray<FString>& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    if (!IsInGameThread() || !IsValid(World) || !World->PersistentLevel)
    {
        Error = TEXT("Open an editor level before preparing its game preview.");
        return false;
    }
    State(); // Start native modification tracking before the first inspection.
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FString> Pending;
    TMap<FString, FString> RequiredBy;
    const auto Need = [&](const FString& Package, const FString& By)
    {
        if (!P4IsContentPackage(Package) || IsRuntimePackage(Package) || RequiredBy.Contains(Package))
            return;
        RequiredBy.Add(Package, By);
        Pending.Add(Package);
    };
    // Play sends what the open level's actors use, which is also everything the live mirror spawns
    // for them. The game's own placed actors use only the game's content, which is already there.
    for (ULevel* Level : World->GetLevels())
    {
        if (!IsValid(Level) || Level->HasAnyFlags(RF_Transient))
            continue;
        UWorld* LevelWorld = Level->GetTypedOuter<UWorld>();
        const TSet<FName> Imported = LevelWorld && LevelWorld->PersistentLevel == Level
            ? P4ImportedLevelActors(LevelWorld)
            : TSet<FName>();
        for (AActor* Actor : Level->Actors)
        {
            if (!IsValid(Actor) || Actor->bIsEditorPreviewActor || Imported.Contains(Actor->GetFName()))
                continue;
            const FString By = Actor->GetActorLabel();
            Need(Actor->GetClass()->GetOutermost()->GetName(), By);
            for (const FString& Dependency : UP4AuthoringTools::NativeAssetDependencies(Actor))
                Need(Dependency, By);
        }
    }
    TArray<TStrongObjectPtr<UObject>> Held;
    for (int32 At = 0; At < Pending.Num(); ++At)
    {
        const FString Package = Pending[At];
        const FString By = RequiredBy.FindRef(Package);
        TArray<FAssetData> PackageAssets;
        Registry.GetAssetsByPackageName(FName(*Package), PackageAssets);
        UObject* Asset = LoadObject<UObject>(nullptr, *P4ObjectPathOf(Package), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Asset && PackageAssets.Num())
            Asset = PackageAssets[0].GetAsset();
        if (!IsValid(Asset))
        {
            // A game path that is not in the project is checked against the game's own files by export.
            if (Package.StartsWith(TEXT(P4_GAME_ASSET_ROOT "/"), ESearchCase::IgnoreCase) &&
                !FindPackage(nullptr, *Package) && !FPackageName::DoesPackageExist(Package))
                continue;
            Error = FString::Printf(TEXT("%s uses %s, which could not be loaded for Play."), *By, *Package);
            return false;
        }
        Held.Emplace(Asset);
        if (P4IsGamePreviewGameSource(Asset))
            continue;
        const FP4OriginalStatus Status = P4QueryOriginalAsset(Asset);
        if (Status.State != EP4OriginalState::NotImported)
        {
            Error = Package + TEXT(" has unresolved original provenance. ") + Status.Error;
            return false;
        }
        FString Why;
        if (!P4IsCreatorAsset(Asset, &Why))
        {
            Error = FString::Printf(TEXT("%s uses %s, which can't go into Play. %s"), *By, *Package, *Why);
            return false;
        }
        // A level of the creator's own is prepared as the open scratch world, never as an asset.
        if (Asset->IsA<UWorld>())
            continue;
        if (Out.Num() >= 4096)
        {
            Error = TEXT("The open level uses more than 4096 packages of your own. Play supports up to 4096.");
            return false;
        }
        Out.Add(Package);
        const FString Name = FPackageName::GetShortName(Package);
        TArray<FName> Dependencies;
        Registry.GetDependencies(FName(*Package), Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
        for (FName Dependency : Dependencies)
            Need(Dependency.ToString(), Name);
        for (const FString& Dependency : UP4AuthoringTools::NativeAssetDependencies(Asset))
            Need(Dependency, Name);
    }
    Out.Sort();
    return true;
}

bool P4GatherGamePreviewContent(
    FP4GamePreviewContentPlan& Out, FString& Error, const FP4GamePreviewStartResult* FrozenStart)
{
    Out = {};
    Error.Reset();
    if (!IsInGameThread() || !GEditor)
    {
        Error = TEXT("Preview content requires the editor thread.");
        return false;
    }
    State(); // Start native modification tracking before the first inspection.
    FP4GamePreviewStartResult SelectedStart;
    UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
    TArray<TStrongObjectPtr<UPackage>> HeldPackages;
    if (EditorWorld)
    {
        // Only the open level's closure goes into Play. Unrelated assets elsewhere in the project,
        // exportable or not, never block Play on a game map.
        if (!P4GatherGamePreviewCreatorClosure(EditorWorld, Out.SourcePackages, Error))
            return false;
        for (const FString& Package : Out.SourcePackages)
            if (UPackage* Loaded = FindPackage(nullptr, *Package))
                HeldPackages.Emplace(Loaded);
        FP4GamePreviewWorldPlan World;
        if (!P4GatherGamePreviewWorld(EditorWorld, World, Error))
            return false;
        if (World.Scratch)
        {
            Out.WorldSourceObjectPath = World.WorldSourceObjectPath;
            Out.EnginePackages = World.EnginePackages;
            Out.DerivedPackages = World.DerivedPackages;
            for (const FString& Package : World.SourcePackages)
                Out.SourcePackages.AddUnique(Package);
            Out.SourcePackages.Sort();
            FString StartSource;
            if (FrozenStart)
            {
                if (FrozenStart->World.Get() != EditorWorld || !FrozenStart->HasFloor ||
                    FrozenStart->Transform.ContainsNaN() || FrozenStart->FloorPoint.ContainsNaN() ||
                    FrozenStart->FloorNormal.ContainsNaN())
                {
                    Error = TEXT("The selected preview start no longer belongs to this editor world.");
                    return false;
                }
                SelectedStart = *FrozenStart;
            }
            else if (!P4GamePreviewStart(EditorWorld, true, SelectedStart, StartSource, Error))
                return false;
            Out.StartTransform = SelectedStart.Transform;
            Out.StartFloorPoint = SelectedStart.FloorPoint;
            Out.StartFloorNormal = SelectedStart.FloorNormal;
            Out.HasStartFloor = SelectedStart.HasFloor;
        }
    }
    FString Evidence = TEXT("kadan-preview-content/4\n");
    if (!Out.WorldSourceObjectPath.IsEmpty())
        Evidence += SelectedStart.FingerprintText() + TEXT("\n");
    if (Out.SourcePackages.Num())
    {
        FString Paks, Retoc, Repak, GameFiles, ShaderArchive;
        P4KadanSettings::Read(P4KadanSettings::GamePaks, Paks);
        P4KadanSettings::Read(P4KadanSettings::Retoc, Retoc);
        P4KadanSettings::Read(P4KadanSettings::Repak, Repak);
        P4KadanSettings::Read(P4KadanSettings::GameFolder, GameFiles);
        P4KadanSettings::Read(P4KadanSettings::ShaderArchive, ShaderArchive);
        Evidence += TEXT("game-files ") + GameFiles + TEXT("\n");
        for (const FString& File :
            {Paks / (P4GameContainerIn(Paks) + TEXT(".utoc")), Paks / (P4GameContainerIn(Paks) + TEXT(".pak")), Retoc, Repak})
        {
            const FFileStatData Stat = IFileManager::Get().GetStatData(*File);
            Evidence += FString::Printf(TEXT("%s|%lld|%lld\n"), *File, Stat.FileSize, Stat.ModificationTime.GetTicks());
        }
        if (!ShaderArchive.IsEmpty())
        {
            const FFileStatData Stat = IFileManager::Get().GetStatData(*ShaderArchive);
            Evidence += FString::Printf(
                TEXT("shader-archive %s|%lld|%lld\n"), *ShaderArchive, Stat.FileSize, Stat.ModificationTime.GetTicks());
        }
    }
    for (const FString& Package : Out.SourcePackages)
    {
        FString Digest;
        if (!PackageDigest(FindPackage(nullptr, *Package), Digest, Error, Out.WorldSourceObjectPath))
            return false;
        Evidence += Package + TEXT(" ") + Digest + TEXT("\n");
    }
    if (!P4HashUtf8(Evidence, Out.Fingerprint))
    {
        Error = TEXT("Cannot fingerprint the preview content plan.");
        return false;
    }
    return true;
}

bool P4CookGamePreviewContent(const FP4GamePreviewContentPlan& Plan, FP4GamePreviewContentResult& Out, FString& Error)
{
    Out = {};
    Error.Reset();
    if (!IsInGameThread() || !GEditor || Plan.Fingerprint.IsEmpty())
    {
        Error = TEXT("Gather preview content on the editor thread before cooking.");
        return false;
    }
    Out.Fingerprint = Plan.Fingerprint;
    if (!Plan.SourcePackages.Num())
        return true;
    // The game's reservation names the world and the scene. The part they share is the area's own name,
    // which the game's name-based lookups use.
    FString AreaName;
    if (!Plan.WorldSourceObjectPath.IsEmpty())
    {
        if (!Plan.HasStartFloor)
        {
            Error = TEXT("The scratch preview has no validated ground contact for its base.");
            return false;
        }
        const auto Plain = [](const FString& Name)
        {
            bool Valid = !Name.IsEmpty() && Name.Len() <= 64;
            for (const TCHAR Character : Name)
                Valid = Valid && (FChar::IsAlnum(Character) || Character == TEXT('_'));
            return Valid;
        };
        AreaName = P4GamePreviewNameToken(Plan.WorldRuntimeName, Plan.SceneRuntimeName);
        if (!Plain(Plan.WorldRuntimeName) || !Plain(Plan.SceneRuntimeName) || AreaName.Len() < 3)
        {
            Error = TEXT("Reserve a game preview area before preparing a scratch level.");
            return false;
        }
    }
    FPreviewContentState& Cache = State();
    if (Cache.Cached.Fingerprint == Plan.Fingerprint && ResultExists(Cache.Cached) &&
        (Plan.WorldSourceObjectPath.IsEmpty()
                ? Cache.Cached.WorldObjectPath.IsEmpty()
                : FPackageName::ObjectPathToObjectName(Cache.Cached.WorldObjectPath) == Plan.WorldRuntimeName))
    {
        Out = Cache.Cached;
        Out.FromCache = true;
        return true;
    }
    FP4ExportModRequest Request;
    Request.Mod.Id = TEXT(P4_PREVIEW_MOD_ID_PREFIX) + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    Request.Mod.Name = TEXT("Kadan live preview");
    Request.Mod.Version = TEXT("0.0.0");
    const FString Root = PreviewDirectory();
    Request.ProjectDirectory = Root / TEXT("export-project");
    Request.OutputDirectory = Root / TEXT("output");
    Request.WorkDirectory = PreviewWorkDirectory() / TEXT("work");
    P4KadanSettings::Read(P4KadanSettings::GamePaks, Request.GamePaksDirectory);
    P4KadanSettings::Read(P4KadanSettings::GameFolder, Request.ExtractedGameDirectory);
    P4KadanSettings::Read(P4KadanSettings::Retoc, Request.Tools.Retoc);
    P4KadanSettings::Read(P4KadanSettings::Repak, Request.Tools.Repak);
    P4KadanSettings::Read(P4KadanSettings::ShaderArchive, Request.ShaderArchiveFile);
    if (Request.GamePaksDirectory.IsEmpty() || Request.Tools.Retoc.IsEmpty() || Request.Tools.Repak.IsEmpty())
    {
        Error = TEXT("To prepare imported assets for Play, set Game Paks, retoc and repak in Kadan Settings.");
        return false;
    }
    FP4GamePreviewContentPlan Prepared = Plan;
    if (Plan.EnginePackages.Num())
    {
        FP4ExportGameInventory Game;
        if (!P4ReadExportGameInventory(Request.Tools, Request.GamePaksDirectory, Root / TEXT("inventory"), Game, Error))
            return false;
        // The registry census lists /Game assets; the actual IoStore census
        // also contains engine packages. Leave those immutable references at
        // their existing runtime identities instead of recompiling them.
        for (const FString& Package : Plan.EnginePackages)
            if (Game.PackageIds().Contains(P4ExportPackageId(Package)))
            {
                Prepared.EnginePackages.Remove(Package);
                Prepared.SourcePackages.Remove(Package);
            }
    }
    FP4ModNames Names;
    if (!P4ModDerivedNames(Request.Mod, Names, Error) ||
        !P4WriteModDescriptor(Request.ProjectDirectory, Request.Mod, nullptr, Error))
        return false;
    Cache.RuntimeRoots.Add(Names.PackageRoot);
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Request.ExtractedGameDirectory;
    Settings.GamePaks = Request.GamePaksDirectory;
    Settings.Retoc = Request.Tools.Retoc;
    // A new level is built from the game's template content and named after its reserved area.
    FP4GamePreviewTemplate Template;
    FString GeometryName;
    if (!Plan.WorldSourceObjectPath.IsEmpty())
    {
        if (!P4ReadGamePreviewTemplate(Settings, Template, Error))
            return false;
        GeometryName = P4GamePreviewRename(
            FPackageName::GetShortName(Template.GeometryStream), Template.WorldToken, AreaName);
    }
    struct FGenerationOutput
    {
        const FP4ExportModRequest& Request;
        FString Root;
        bool Keep = false;
        ~FGenerationOutput()
        {
            RemoveOwnedTree(Request.WorkDirectory / (Request.Mod.Id + TEXT(".export-work")), PreviewWorkDirectory());
            if (!Keep)
            {
                RemoveOwnedTree(Request.OutputDirectory / Request.Mod.Id, Root);
                IFileManager::Get().Delete(
                    *(Request.ProjectDirectory / TEXT("Config/KadanMods") / (Request.Mod.Id + TEXT(".json"))));
            }
        }
    } Generation{Request, Root};
    TGuardValue<bool> Staging(Cache.Staging, true);
    FP4GamePreviewEngineCopies EngineCopies;
    FP4GamePreviewDerivedCopies DerivedCopies;
    FP4GamePreviewMaterialInstances MaterialInstances;
    FCloneScope Clones(Error);
    if (!Clones.Prepare(Prepared, Names.PackageRoot, GeometryName, Error))
        return false;
    UWorld* AuthoredWorld = Plan.WorldSourceObjectPath.IsEmpty()
        ? nullptr
        : FindObject<UWorld>(nullptr, *Plan.WorldSourceObjectPath);
    for (const auto& Pair : Clones.Replacements)
    {
        if (Prepared.EnginePackages.Contains(Pair.Key->GetOutermost()->GetName()) &&
            Pair.Key->GetOuter() == Pair.Key->GetOutermost() && Pair.Key->IsAsset())
            if (!EngineCopies.Add(Pair.Value, Pair.Key, Error))
                return false;
        if ((Pair.Key->IsA<UMapBuildDataRegistry>() || Pair.Key->IsA<UTexture>()) &&
            Prepared.DerivedPackages.Contains(Pair.Key->GetOutermost()->GetName()) &&
            P4IsGamePreviewDerivedSource(Pair.Key, AuthoredWorld))
            if (!DerivedCopies.Add(Pair.Value, Pair.Key, AuthoredWorld, Error))
                return false;
    }
    // The default sky stores a real MID in both its concrete Blueprint
    // variable and mesh material slot. Retain that type and its overrides;
    // only a proven engine parent may be staged to the generated runtime MIC.
    if (AuthoredWorld)
        for (const auto& Pair : Clones.Replacements)
            if (Pair.Key->GetClass() == UMaterialInstanceDynamic::StaticClass() && Pair.Key->IsIn(AuthoredWorld))
                if (UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Pair.Value))
                    if (P4IsTrustedGamePreviewEngineCopy(Instance->Parent))
                        if (!MaterialInstances.Add(Instance, Pair.Key, AuthoredWorld, Error))
                            return false;
    TSet<UMaterial*> EngineMaterials;
    for (const auto& Pair : Clones.Replacements)
        if (UMaterial* Graph = Cast<UMaterial>(Pair.Value))
            if (P4IsTrustedGamePreviewEngineCopy(Graph))
                EngineMaterials.Add(Graph);
    for (UMaterial* Graph : EngineMaterials)
        if (!Clones.NormalizeEngineMaterialSamples(Graph, Error))
            return false;
    FP4GamePreviewSceneAssets Scene;
    FP4GamePreviewBootstrapResult Bootstrap;
    if (!Plan.WorldSourceObjectPath.IsEmpty())
    {
        UWorld* SourceWorld = FindObject<UWorld>(nullptr, *Plan.WorldSourceObjectPath);
        UWorld* GeometryWorld = Cast<UWorld>(Clones.Replacements.FindRef(SourceWorld));
        if (!GeometryWorld)
        {
            Error = TEXT("The scratch geometry world could not be cloned.");
            return false;
        }
        FP4GamePreviewNavigation Navigation;
        Clones.Phase = TEXT("building authored navigation");
        if (!P4BuildGamePreviewNavigation(GeometryWorld, Settings, Template.World, Navigation, Error))
            return false;
        Clones.Phase = TEXT("preparing game world settings");
        if (!P4PrepareGamePreviewWorld(GeometryWorld, SourceWorld, Error))
            return false;
        Clones.Phase = TEXT("refreshing replaced world settings references");
        if (!Clones.RefreshWorldSettings(SourceWorld, GeometryWorld, Error))
            return false;
        Clones.Phase = TEXT("building game scene bootstrap");
        if (!P4BuildGamePreviewBootstrap(Settings, Template, Names.PackageRoot, Plan.WorldRuntimeName, AreaName,
                GeometryWorld->GetOutermost()->GetName(), Plan.StartTransform, Plan.StartFloorPoint,
                Plan.StartFloorNormal, Bootstrap, Error))
            return false;
        bool Patched = false;
        for (FP4GamePreviewBootstrapPackage& Package : Bootstrap.Packages)
            if (Package.Package == Bootstrap.WorldPackage)
            {
                if (!P4PatchGamePreviewNavigation(Package.Header, Package.Body, Navigation, Error))
                    return false;
                Patched = true;
            }
        if (!Patched)
        {
            Error = TEXT("The scratch bootstrap has no world for its navigation.");
            return false;
        }
        const FString RootWorld = P4ObjectPathOf(Bootstrap.WorldPackage);
        Clones.Phase = TEXT("creating scene preload assets");
        // The world and scene take the reserved names; the counts are the template's, renamed for the area.
        FP4GamePreviewSceneRecipe Recipe;
        Recipe.World = Plan.WorldRuntimeName;
        Recipe.Scene = Plan.SceneRuntimeName;
        Recipe.SceneClass = Template.SceneClass;
        Recipe.Counts = P4GamePreviewRename(FPackageName::GetShortName(Template.Counts), Template.WorldToken, AreaName);
        Recipe.CountsClass = Template.CountsClass;
        Recipe.ParentScene = Template.ParentScene;
        Recipe.CountsVersion = Template.CountsVersion;
        if (!Scene.Create(Names.PackageRoot, RootWorld, Recipe, Error, Bootstrap.LoadAssets))
            return false;
        Request.PreviewOnly = true;
        Request.PreviewWorldObjectPath = RootWorld;
        Request.PreparePreviewPackages = [&](const FString& Stage, TArray<FString>& Extra, FString& Why)
        { return P4WriteGamePreviewBootstrap(Bootstrap, Stage, Extra, Why); };
    }
    Request.Roots.Add({EP4ExportRootKind::Folder, Names.PackageRoot});
    FP4ExportModResult Exported;
    Clones.Phase = TEXT("exporting prepared content");
    if (!P4ExportMod(Request, Exported, Error))
    {
        // Name what cannot go into Play, by the creator's own asset names.
        if (Exported.Refused.Num() && (Error.IsEmpty() || Error == TEXT("Selection has refused packages")))
        {
            TArray<FString> Named = Exported.Refused;
            if (Named.Num() > 5)
            {
                const int32 More = Named.Num() - 5;
                Named.SetNum(5);
                Named.Add(FString::Printf(TEXT("%d more can't be shipped either."), More));
            }
            Error = FString::Join(Named, TEXT(" "));
        }
        Error = Clones.SourceNames(Error);
        return false;
    }
    if (!Exported.Published)
    {
        Error = TEXT("The preview content export was not published.");
        return false;
    }
    FP4GamePreviewContentResult Result;
    Result.Fingerprint = Plan.Fingerprint;
    Result.PackageRoot = Names.PackageRoot;
    Result.WorldObjectPath = Request.PreviewWorldObjectPath;
    Result.SceneObjectPath = Scene.SceneObjectPath();
    Result.ContainerStem = Names.Container + TEXT("_P");
    const FString Metadata = Exported.Directory / TEXT("romfs/tsuchi/mods") / Request.Mod.Id;
    Result.PakFile = Metadata / (Result.ContainerStem + TEXT(".pak"));
    Result.UtocFile = Metadata / (Result.ContainerStem + TEXT(".utoc"));
    Result.UcasFile = Metadata / (Result.ContainerStem + TEXT(".ucas"));
    FString ManifestText;
    TSharedPtr<FJsonObject> Manifest;
    if (!FFileHelper::LoadFileToString(ManifestText, *(Metadata / TEXT("mod.json"))) ||
        !P4ParseJson(ManifestText, Manifest) || !Manifest)
    {
        Error = TEXT("The preview export manifest could not be read.");
        return false;
    }
    const TSharedPtr<FJsonObject>* Shaders = nullptr;
    if (Manifest->TryGetObjectField(TEXT("shaders"), Shaders) && Shaders && Shaders->IsValid())
    {
        Result.ShaderFile = Metadata / (*Shaders)->GetStringField(TEXT("archive"));
        // The runtime spool gives part 3 this exact name; shader records and
        // hashes stay unchanged from native export.
        (*Shaders)->SetStringField(TEXT("archive"), Result.ContainerStem + TEXT(".shaders"));
        FJsonSerializer::Serialize(Shaders->ToSharedRef(), TJsonWriterFactory<>::Create(&Result.ShaderMetadataJson));
    }
    for (const auto& Pair : Clones.Replacements)
    {
        const FString Source = Pair.Key->GetPathName(), Clone = Pair.Value->GetPathName();
        const FString* Runtime = Exported.RuntimePaths.Find(Clone);
        Result.RuntimePaths.Add(Source, Runtime ? *Runtime : Clone);
    }
    if (!ResultExists(Result))
    {
        Error = TEXT("The preview export did not produce a complete bundle.");
        return false;
    }
    const FString PreviousId = Cache.PreviousId.IsEmpty() ? PreviousOutput(Root) : Cache.PreviousId;
    if (!RememberOutput(Root, Request.Mod.Id))
    {
        Error = TEXT("Cannot retain the prepared preview content identity for recovery.");
        return false;
    }
    // Retain the previous successful cook until its replacement passes the full
    // native exporter. Only exact identities this editor created are eligible.
    if (!PreviousId.IsEmpty() && PreviousId != Request.Mod.Id)
    {
        RemoveOwnedTree(Request.OutputDirectory / PreviousId, Root);
        RemoveOwnedTree(Request.WorkDirectory / (PreviousId + TEXT(".export-work")), PreviewWorkDirectory());
        IFileManager::Get().Delete(
            *(Request.ProjectDirectory / TEXT("Config/KadanMods") / (PreviousId + TEXT(".json"))));
    }
    Generation.Keep = true;
    Cache.PreviousId = Request.Mod.Id;
    Cache.Cached = Result;
    Out = MoveTemp(Result);
    return true;
}

namespace
{
FAutoConsoleCommand PreviewContentCommand(TEXT("Kadan.PreviewContent"),
    TEXT("Prepare native live content and write a JSON report: <report.json> [gather|<reserved world> <reserved scene>]."),
    FConsoleCommandWithArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args)
        {
            const FString Input = FString::Join(Args, TEXT(" "));
            const TCHAR* Cursor = *Input;
            const FString ReportFile = FParse::Token(Cursor, false);
            const FString Mode = FParse::Token(Cursor, false);
            const FString SceneName = FParse::Token(Cursor, false);
            if (ReportFile.IsEmpty())
            {
                UE_LOG(LogTemp, Warning, TEXT("Kadan.PreviewContent needs a JSON report filename."));
                return;
            }
            FP4GamePreviewContentPlan Plan;
            FP4GamePreviewContentResult Result;
            FString Error;
            const bool Gathered = P4GatherGamePreviewContent(Plan, Error);
            const bool Inspection = Mode == TEXT("gather");
            if (!Mode.IsEmpty() && !Inspection)
            {
                Plan.WorldRuntimeName = Mode;
                Plan.SceneRuntimeName = SceneName;
            }
            const bool Ok = Gathered && (Inspection || P4CookGamePreviewContent(Plan, Result, Error));
            auto Report = MakeShared<FJsonObject>();
            Report->SetBoolField(TEXT("ok"), Ok);
            Report->SetStringField(TEXT("error"), Error);
            Report->SetStringField(TEXT("fingerprint"), Plan.Fingerprint);
            TArray<TSharedPtr<FJsonValue>> Packages, Warnings;
            for (const FString& Package : Plan.SourcePackages)
                Packages.Add(MakeShared<FJsonValueString>(Package));
            for (const FString& Warning : Plan.Warnings)
                Warnings.Add(MakeShared<FJsonValueString>(Warning));
            Report->SetArrayField(TEXT("packages"), Packages);
            Report->SetArrayField(TEXT("warnings"), Warnings);
            Report->SetStringField(TEXT("container"), Result.ContainerStem);
            Report->SetStringField(TEXT("root"), Result.PackageRoot);
            Report->SetStringField(TEXT("world_source"), Plan.WorldSourceObjectPath);
            Report->SetStringField(TEXT("world"), Result.WorldObjectPath);
            Report->SetStringField(TEXT("scene"), Result.SceneObjectPath);
            Report->SetStringField(TEXT("pak"), Result.PakFile);
            Report->SetStringField(TEXT("utoc"), Result.UtocFile);
            Report->SetStringField(TEXT("ucas"), Result.UcasFile);
            Report->SetStringField(TEXT("shaders"), Result.ShaderFile);
            Report->SetStringField(TEXT("shader_metadata"), Result.ShaderMetadataJson);
            Report->SetBoolField(TEXT("cached"), Result.FromCache);
            auto Aliases = MakeShared<FJsonObject>();
            for (const auto& Alias : Result.RuntimePaths)
                Aliases->SetStringField(Alias.Key, Alias.Value);
            Report->SetObjectField(TEXT("aliases"), Aliases);
            FString Text;
            FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
            if (!FFileHelper::SaveStringToFile(Text, *ReportFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
                UE_LOG(LogTemp, Error, TEXT("Kadan preview content report could not be written: %s"), *ReportFile);
            UE_LOG(LogTemp, Display, TEXT("Kadan preview content: %d packages, %s. %s"), Plan.SourcePackages.Num(),
                Ok ? TEXT("ready") : TEXT("failed"), *Error);
        }));
} //namespace
