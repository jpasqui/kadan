#include "P4CreatorSource.h"
#include "P4Common.h"
#include "P4GameNames.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "P4OriginalImportSave.h"
#include "P4OriginalRecord.h"
#include "P4StandInFill.h"
#include "P4KadanReadOnly.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/ImportSubsystem.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/UObjectIterator.h"

namespace P4Kadan
{
void AcceptCompletedCreatorSource(UObject* Asset);
}

struct FP4CreatorSourceAccess
{
    static void Set(UP4CreatorSourceData& Data, const UObject& Asset, const TCHAR* Kind, const FString& Mode)
    {
#if WITH_EDITORONLY_DATA
        Data.Format = 1;
        Data.Kind = FName(Kind);
        Data.SourceMode = FName(*Mode);
        Data.AssetClass = FName(*Asset.GetClass()->GetPathName());
        Data.OriginPackage = NAME_None;
#endif
    }
    static void SetGame(UP4CreatorSourceData& Data, const UObject& Asset, const FString& Package, bool Proxy)
    {
        Set(Data, Asset, Proxy ? TEXT("game-proxy") : TEXT("game-source"),
            Proxy ? TEXT("proxy-reference") : TEXT("original-publication"));
#if WITH_EDITORONLY_DATA
        Data.OriginPackage = FName(*Package);
#endif
    }
    static bool Game(const UP4CreatorSourceData& Data, const UObject& Asset, FString* Package, bool* Proxy)
    {
#if WITH_EDITORONLY_DATA
        if (Data.Format != 1 || Data.AssetClass != FName(*Asset.GetClass()->GetPathName()) ||
            Data.OriginPackage.IsNone())
            return false;
        const bool IsProxy = Data.Kind == TEXT("game-proxy") && Data.SourceMode == TEXT("proxy-reference");
        if (!IsProxy && !(Data.Kind == TEXT("game-source") && Data.SourceMode == TEXT("original-publication")))
            return false;
        if (Package)
            *Package = Data.OriginPackage.ToString();
        if (Proxy)
            *Proxy = IsProxy;
        return true;
#else
        return false;
#endif
    }
    static bool Matches(const UP4CreatorSourceData& Data, const UObject& Asset)
    {
#if WITH_EDITORONLY_DATA
        if (Data.Format != 1 || Data.AssetClass != FName(*Asset.GetClass()->GetPathName()))
            return false;
        if (Data.Kind == TEXT("creator-file"))
            return (Data.SourceMode == TEXT("texture-factory") && Asset.IsA<UTexture2D>()) ||
                (Data.SourceMode == TEXT("fbx-factory") && Asset.IsA<UStaticMesh>()) ||
                (Data.SourceMode == TEXT("fbx-skeletal-factory") && Asset.IsA<USkeletalMesh>()) ||
                (Data.SourceMode == TEXT("fbx-animation-factory") && Asset.IsA<UAnimSequence>()) ||
                (Data.SourceMode == TEXT("fbx-skeleton-output") && Asset.IsA<USkeleton>());
        if (Data.Kind == TEXT("creator-material") && Data.SourceMode == TEXT("material-factory") &&
            Asset.IsA<UMaterial>())
            return true;
        return Data.Kind == TEXT("creator-instance") &&
            (Data.SourceMode == TEXT("instance-builder") || Data.SourceMode == TEXT("instance-factory")) &&
            Asset.IsA<UMaterialInstanceConstant>();
#else
        return false;
#endif
    }
};

namespace
{
const FName SourceKey(TEXT("P4KadanCreatorSource"));
TWeakObjectPtr<UImportSubsystem> ImportSubsystem;
FDelegateHandle PreHandle, PostHandle;
struct FAttempt
{
    TWeakObjectPtr<UFactory> Factory;
    TWeakObjectPtr<UObject> Parent;
    TWeakObjectPtr<UClass> AssetClass;
    FName Name;
    TMap<FName, FString> History;
    bool HadExisting = false, HadCreatorSource = false, WasProxy = false, HadGameSource = false;
    FString GameSourcePackage;
    FString Filename, Mode;
    TSet<FName> ExistingSkeletonPackages;
};
TArray<FAttempt> Attempts;

UMetaData* ExistingMetadata(const UObject* Asset)
{
    if (!IsInGameThread() || !IsValid(Asset))
        return nullptr;
#if WITH_EDITORONLY_DATA
    // Package serialization calls the policy from asset-registry tag gathering. Native
    // object hash lookup is forbidden there (and during GC). Read the package's existing
    // pointer only; do not create or preload metadata to answer a creator-source query.
    UMetaData* Meta = Asset->GetOutermost()->MetaData;
    return Meta && !Meta->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad) ? Meta : nullptr;
#else
    return nullptr;
#endif
}
bool IsProxy(const UObject* Asset)
{
    UMetaData* Meta = ExistingMetadata(Asset);
    return Meta && FString(Meta->GetValue(Asset, TEXT("P4BlueprintStandIn"))) == TEXT("1");
}
void Record(UObject* Asset, const TCHAR* Kind, const FString& Mode)
{
    if (!IsValid(Asset) || P4ImportPublication::IsImporting() || IsProxy(Asset))
        return;
    IInterface_AssetUserData* Owner = Cast<IInterface_AssetUserData>(Asset);
    if (!Owner)
        return;
    Owner->RemoveUserDataOfClass(UP4CreatorSourceData::StaticClass());
    UP4CreatorSourceData* Data = NewObject<UP4CreatorSourceData>(Asset);
    FP4CreatorSourceAccess::Set(*Data, *Asset, Kind, Mode);
    Owner->AddAssetUserData(Data);
    // This text is a diagnostic of the operation, not a certificate or the duplicate/rename proof.
    TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
    Value->SetNumberField(TEXT("format"), 1);
    Value->SetStringField(TEXT("package"), Asset->GetOutermost()->GetName());
    Value->SetStringField(TEXT("asset"), Asset->GetPathName());
    Value->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
    Value->SetStringField(TEXT("kind"), Kind);
    Value->SetStringField(TEXT("source_mode"), Mode);
    FString Text = P4CondensedJson(Value);
    Asset->GetOutermost()->GetMetaData()->SetValue(Asset, SourceKey, *Text);
    Asset->MarkPackageDirty();
}
FString FullFilename(const FString& Filename)
{
    FString Out = FPaths::ConvertRelativePathToFull(Filename);
    FPaths::NormalizeFilename(Out);
    FPaths::CollapseRelativeDirectories(Out);
    return Out;
}
bool SourceMatches(const UAssetImportData* Data, const FString& Filename)
{
    if (!Data)
        return false;
    TArray<FString> Files;
    Data->ExtractFilenames(Files);
    return Files.Num() == 1 && FullFilename(Files[0]).Equals(Filename, ESearchCase::IgnoreCase);
}
void BeforeImport(UFactory* Factory, UClass* Class, UObject* Parent, const FName& Name, const TCHAR*)
{
    if (!IsInGameThread())
        return;
    Attempts.RemoveAll([&](const FAttempt& Item) { return !Item.Factory.IsValid() || Item.Factory.Get() == Factory; });
    if (P4ImportPublication::IsImporting() || !IsValid(Factory) || !IsValid(Parent))
        return;
    FString Mode;
    if (Factory->IsA<UTextureFactory>() && Class == UTexture2D::StaticClass())
        Mode = TEXT("texture-factory");
    else if (UFbxFactory* Fbx = Cast<UFbxFactory>(Factory))
    {
        if (Fbx->ImportUI && Fbx->ImportUI->MeshTypeToImport == FBXIT_Animation)
        {
            Mode = TEXT("fbx-animation-factory");
            Class = UAnimSequence::StaticClass();
        }
        else if (Fbx->ImportUI && Fbx->ImportUI->MeshTypeToImport == FBXIT_SkeletalMesh)
        {
            Mode = TEXT("fbx-skeletal-factory");
            Class = USkeletalMesh::StaticClass();
        }
        else if (Class == UStaticMesh::StaticClass())
            Mode = TEXT("fbx-factory");
        else
            return;
    }
    else
        return;
    const FString Filename = UFactory::GetCurrentFilename();
    if (Filename.IsEmpty() || IFileManager::Get().FileSize(*Filename) <= 0)
        return;
    FAttempt Attempt;
    Attempt.Factory = Factory;
    Attempt.Parent = Parent;
    Attempt.AssetClass = Class;
    Attempt.Name = Name;
    if (Mode == TEXT("fbx-skeletal-factory"))
    {
        for (TObjectIterator<USkeleton> It; It; ++It)
            Attempt.ExistingSkeletonPackages.Add(It->GetOutermost()->GetFName());
        TArray<FAssetData> Skeletons;
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
            .Get()
            .GetAssetsByClass(USkeleton::StaticClass()->GetFName(), Skeletons);
        for (const FAssetData& Skeleton : Skeletons)
            Attempt.ExistingSkeletonPackages.Add(Skeleton.PackageName);
    }
    // Stock file factories may reconstruct the UObject at this exact identity, discarding
    // its metadata. Retain only existing game-source history, verbatim, before that happens.
    // The current stand-in flag is a representation role, not history to restore.
    UObject* Existing = FindObjectFast<UObject>(Parent, Name);
    Attempt.HadExisting = Existing != nullptr;
    Attempt.HadCreatorSource = Existing && Existing->GetClass() == Class && P4HasCreatorSource(Existing);
    Attempt.HadGameSource = Existing && P4HasIntrinsicGameSource(Existing, &Attempt.GameSourcePackage);
    Attempt.WasProxy = Existing && (IsProxy(Existing) || P4HasIntrinsicGameProxy(Existing));
    if (Existing && Existing->GetClass() == Class)
        if (UMetaData* Meta = ExistingMetadata(Existing))
            for (const TCHAR* Key : {TEXT("P4OriginalRecord"), TEXT("P4NativeTextureBaseline"),
                     TEXT("P4NativeMeshBaseline"), TEXT("P4NativeObjectBaseline"), TEXT("P4NativeObjectValuesNotHeld"),
                     TEXT("P4NativeLevelBaseline"), TEXT("P4NativeLevelImported"), TEXT("P4NativeLevelLighting"),
                     TEXT("P4NativeLevelRunLines"), TEXT("P4GameRows"), TEXT("P4KadanCopiedGameAsset")})
                if (Meta->HasValue(Existing, Key))
                    Attempt.History.Add(FName(Key), Meta->GetValue(Existing, Key));
    Attempt.Filename = FullFilename(Filename);
    Attempt.Mode = Mode;
    Attempts.Add(MoveTemp(Attempt));
}
void AfterImport(UFactory* Factory, UObject* Asset)
{
    // Match a completed operation to its own pre-import event. No inherited ImportData
    // or asset-registry notification is sufficient to establish creator source.
    const int32 Index = Attempts.FindLastByPredicate(
        [&](const FAttempt& Item) { return Item.Factory.Get() == Factory; });
    if (Index == INDEX_NONE)
        return;
    if (!IsValid(Asset))
    {
        Attempts.RemoveAt(Index);
        return;
    }
    const FAttempt& Pending = Attempts[Index];
    // The FBX factory may publish its auxiliary skeleton before its requested mesh.
    if (Asset->GetOuter() != Pending.Parent.Get() || Asset->GetFName() != Pending.Name ||
        Asset->GetClass() != Pending.AssetClass.Get())
        return;
    FAttempt Attempt = MoveTemp(Attempts[Index]);
    Attempts.RemoveAt(Index);
    if (P4ImportPublication::IsImporting() ||
        !FullFilename(UFactory::GetCurrentFilename()).Equals(Attempt.Filename, ESearchCase::IgnoreCase))
        return;
    if (Attempt.Mode == TEXT("texture-factory"))
    {
        UTexture2D* Texture = Cast<UTexture2D>(Asset);
        if (!Texture || !Texture->Source.IsValid() || !SourceMatches(Texture->AssetImportData, Attempt.Filename))
            return;
    }
    else if (Attempt.Mode == TEXT("fbx-factory"))
    {
        UStaticMesh* Mesh = Cast<UStaticMesh>(Asset);
        const FMeshDescription* Description = Mesh && Mesh->GetNumSourceModels() > 0 ? Mesh->GetMeshDescription(0)
                                                                                     : nullptr;
        if (!Description || Description->Vertices().Num() == 0 || Description->Triangles().Num() == 0 ||
            !SourceMatches(Mesh->AssetImportData, Attempt.Filename))
            return;
    }
    else if (Attempt.Mode == TEXT("fbx-skeletal-factory"))
    {
        USkeletalMesh* Mesh = Cast<USkeletalMesh>(Asset);
        const FSkeletalMeshModel* Model = Mesh ? Mesh->GetImportedModel() : nullptr;
        if (!Mesh || !Mesh->Skeleton || !Model || Model->LODModels.Num() == 0 || Model->LODModels[0].NumVertices <= 0 ||
            Model->LODModels[0].IndexBuffer.Num() < 3 || Mesh->RefSkeleton.GetNum() == 0 ||
            !SourceMatches(Mesh->AssetImportData, Attempt.Filename))
            return;
    }
    else if (Attempt.Mode == TEXT("fbx-animation-factory"))
    {
        const UAnimSequence* Sequence = Cast<UAnimSequence>(Asset);
        if (!Sequence || !Sequence->GetSkeleton() || Sequence->GetRawAnimationData().Num() == 0 ||
            Sequence->GetRawNumberOfFrames() <= 0 || !SourceMatches(Sequence->AssetImportData, Attempt.Filename))
            return;
    }
    // Only this completed full-source factory operation retires the current placeholder role.
    // Original records and copied-game history remain as history.
    UMetaData* Meta = Asset->GetOutermost()->GetMetaData();
    for (const TPair<FName, FString>& History : Attempt.History)
        Meta->SetValue(Asset, History.Key, *History.Value);
    if (Attempt.Mode.StartsWith(TEXT("fbx-")) && Attempt.HadExisting && !Attempt.HadCreatorSource)
    {
        // A new MeshDescription does not prove that an occupied native mesh shed every
        // previous collision/render payload. Only fresh imports and known creator reimports
        // establish creator source; unknown, copied-game and proxy meshes retain their original source role.
        if (Attempt.WasProxy)
            Meta->SetValue(Asset, TEXT("P4BlueprintStandIn"), TEXT("1"));
        if (IInterface_AssetUserData* Owner = Cast<IInterface_AssetUserData>(Asset))
            Owner->RemoveUserDataOfClass(UP4CreatorSourceData::StaticClass());
        Meta->RemoveValue(Asset, SourceKey);
        if (Attempt.HadGameSource)
            P4RecordKnownGameSource(Asset, Attempt.GameSourcePackage, Attempt.WasProxy);
        P4Kadan::UpdateReadOnlyAsset(Asset);
        P4Kadan::Tell(TEXT("Reimporting ") + Asset->GetName() +
            TEXT(" does not make it your own mesh. Import your mesh as a new asset."));
        return;
    }
    Meta->RemoveValue(Asset, TEXT("P4BlueprintStandIn"));
    Record(Asset, TEXT("creator-file"), Attempt.Mode);
    if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Asset))
    {
        USkeleton* Skeleton = Mesh->Skeleton;
        FString ExistingFile;
        if (Skeleton && !Attempt.ExistingSkeletonPackages.Contains(Skeleton->GetOutermost()->GetFName()) &&
            !FPackageName::DoesPackageExist(Skeleton->GetOutermost()->GetName(), nullptr, &ExistingFile) &&
            !P4HasIntrinsicGameSource(Skeleton) && !IsProxy(Skeleton))
        {
            const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
            bool Matches = Ref.GetNum() == Mesh->RefSkeleton.GetNum();
            for (int32 Bone = 0; Matches && Bone < Ref.GetNum(); ++Bone)
                Matches = Ref.GetBoneName(Bone) == Mesh->RefSkeleton.GetBoneName(Bone) &&
                    Ref.GetParentIndex(Bone) == Mesh->RefSkeleton.GetParentIndex(Bone) &&
                    Ref.GetRefBonePose()[Bone].Equals(Mesh->RefSkeleton.GetRefBonePose()[Bone]);
            if (Matches)
                Record(Skeleton, TEXT("creator-file"), TEXT("fbx-skeleton-output"));
        }
    }
    P4Kadan::AcceptCompletedCreatorSource(Asset);
    P4Kadan::UpdateReadOnlyAsset(Asset);
}
} //namespace

bool P4HasIntrinsicGameSource(const UObject* Asset, FString* SourcePackage)
{
    if (SourcePackage)
        SourcePackage->Reset();
    if (!IsInGameThread() || !IsValid(Asset))
        return false;
    IInterface_AssetUserData* Owner = Cast<IInterface_AssetUserData>(const_cast<UObject*>(Asset));
    const UP4CreatorSourceData* Data = Owner
        ? Cast<UP4CreatorSourceData>(Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass()))
        : nullptr;
    return IsValid(Data) && Data->GetOuter() == Asset && !Data->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad) &&
        FP4CreatorSourceAccess::Game(*Data, *Asset, SourcePackage, nullptr);
}
bool P4HasIntrinsicGameProxy(const UObject* Asset)
{
    if (!IsInGameThread() || !IsValid(Asset))
        return false;
    IInterface_AssetUserData* Owner = Cast<IInterface_AssetUserData>(const_cast<UObject*>(Asset));
    const UP4CreatorSourceData* Data = Owner
        ? Cast<UP4CreatorSourceData>(Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass()))
        : nullptr;
    bool Proxy = false;
    return IsValid(Data) && Data->GetOuter() == Asset && !Data->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad) &&
        FP4CreatorSourceAccess::Game(*Data, *Asset, nullptr, &Proxy) && Proxy;
}
bool P4RecordKnownGameSource(UObject* Asset, const FString& OriginalPackage, bool Proxy)
{
    if (!IsInGameThread() || !IsValid(Asset) || OriginalPackage.IsEmpty() || GIsSavingPackage ||
        IsGarbageCollecting() ||
        Asset->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_NeedInitialization))
        return false;
    const FUObjectThreadContext& Context = FUObjectThreadContext::Get();
    if (Context.IsRoutingPostLoad || Context.IsInConstructor || P4HasCreatorSource(Asset))
        return false;
    IInterface_AssetUserData* Owner = Cast<IInterface_AssetUserData>(Asset);
    if (!Owner)
        return false;
    // A metadata flag disappearing or an ordinary copy cannot rewrite an existing
    // negative fact. Only an actual verified fill under its rollback guard may transition it.
    if (P4HasIntrinsicGameSource(Asset))
        return true;
    Owner->RemoveUserDataOfClass(UP4CreatorSourceData::StaticClass());
    UP4CreatorSourceData* Data = NewObject<UP4CreatorSourceData>(Asset);
    FP4CreatorSourceAccess::SetGame(*Data, *Asset, OriginalPackage, Proxy);
    Owner->AddAssetUserData(Data);
    // Verified loaded-original upgrades are in memory only. Publication already has its
    // ordinary save boundary, and a native duplicate serializes its own instanced role.
    return true;
}

bool P4PrepareVerifiedGameFillSource(UObject* Asset, const FP4StandInFill& Guard, FString& Error)
{
    Error.Reset();
    if (!Guard.OwnsTarget(Asset) || !IsInGameThread() || !IsValid(Asset) || GIsSavingPackage || IsGarbageCollecting() ||
        Asset->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_NeedInitialization))
    {
        Error = TEXT("verified-game-fill-source-needs-owned-ready-target");
        return false;
    }
    const FUObjectThreadContext& Context = FUObjectThreadContext::Get();
    if (Context.IsRoutingPostLoad || Context.IsInConstructor)
    {
        Error = TEXT("verified-game-fill-source-routing-unsafe");
        return false;
    }
    FP4OriginalRecord Record;
    if (!P4ReadOriginalRecord(Asset, Record, Error))
        return false;
    if (Record.Kind == TEXT("placeholder") || Record.Fidelity == TEXT("stand-in"))
    {
        Error = TEXT("verified-game-fill-source-still-placeholder");
        return false;
    }
    IInterface_AssetUserData* Owner = Cast<IInterface_AssetUserData>(Asset);
    if (!Owner)
        return true;
    UP4CreatorSourceData* Data = Cast<UP4CreatorSourceData>(
        Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass()));
    if (!Data || Data->GetOuter() != Asset || !FP4CreatorSourceAccess::Game(*Data, *Asset, nullptr, nullptr))
    {
        if (!P4RecordKnownGameSource(Asset, Record.Package, false))
        {
            Error = TEXT("verified-game-fill-source-not-game-data");
            return false;
        }
        Data = Cast<UP4CreatorSourceData>(Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass()));
    }
    if (!IsValid(Data) || Data->GetOuter() != Asset || !FP4CreatorSourceAccess::Game(*Data, *Asset, nullptr, nullptr))
    {
        Error = TEXT("verified-game-fill-source-not-owned");
        return false;
    }
    // Existing data is changed in place: FP4StandInFill snapshots all owned UObjects,
    // including these private properties, and holds them through GC. Newly created data
    // is also removed by that guard on failure. A successful ordinary save persists this
    // current role before Commit; no second save or later tick is needed.
    FP4CreatorSourceAccess::SetGame(*Data, *Asset, Record.Package, false);
    return true;
}

bool P4HasCreatorSource(const UObject* Asset, FString* Reason)
{
    if (Reason)
        Reason->Reset();
    const auto No = [&](const TCHAR* Why)
    {
        if (Reason)
            *Reason = Why;
        return false;
    };
    if (!IsInGameThread() || !IsValid(Asset))
        return No(TEXT("Creator source needs a loaded asset on the editor thread."));
    if (IsProxy(Asset) || P4HasIntrinsicGameProxy(Asset))
        return No(TEXT("An unrecovered game reference is not a creator asset."));
    // Unreal duplicates instanced AssetUserData together with its asset, including its outer.
    // Source import data and metadata absence are never used as positive evidence here.
    IInterface_AssetUserData* Owner = Cast<IInterface_AssetUserData>(const_cast<UObject*>(Asset));
    const UP4CreatorSourceData* Data = Owner
        ? Cast<UP4CreatorSourceData>(Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass()))
        : nullptr;
    if (!Data || Data->GetOuter() != Asset || Data->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad) ||
        !FP4CreatorSourceAccess::Matches(*Data, *Asset))
        return No(TEXT(
            "This asset has no supported creator source. Import your own texture or FBX, or create a supported material or material instance."));
    return true;
}

bool P4IsGameClass(const UClass* Class)
{
    if (!Class)
        return false;
    if (Class->GetOutermost()->GetName() == TEXT(P4_GAME_SCRIPT_PACKAGE))
        return true;
    const UBlueprintGeneratedClass* Made = Cast<UBlueprintGeneratedClass>(Class);
    return Made && IsValid(Made->ClassGeneratedBy) && !P4IsCreatorAsset(Made->ClassGeneratedBy);
}

bool P4IsCreatorAsset(const UObject* Asset, FString* Reason)
{
    if (Reason)
        Reason->Reset();
    const auto No = [&](const TCHAR* Why)
    {
        if (Reason)
            *Reason = Why;
        return false;
    };
    if (!IsInGameThread() || !IsValid(Asset))
        return No(TEXT("Creator source needs a loaded asset on the editor thread."));
    if (UMetaData* Meta = ExistingMetadata(Asset))
        if (Meta->HasValue(Asset, TEXT("P4KadanCopiedGameDataFailure")))
            return No(TEXT(
                "The saved game values in this copy could not be verified. Make a new copy from the game asset before exporting it."));
    if (IsProxy(Asset) || P4HasIntrinsicGameProxy(Asset))
        return No(TEXT("An unrecovered game reference is not a creator asset."));
    if (P4HasCreatorSource(Asset))
        return true;
    FString Origin;
    if (P4HasIntrinsicGameSource(Asset, &Origin) && Origin == Asset->GetOutermost()->GetName())
        return No(
            TEXT("This is the game's own asset. Make a copy of it in your mod's folder to change and export it."));
    FP4OriginalRecord Record;
    FString Error;
    if (P4ReadOriginalRecord(const_cast<UObject*>(Asset), Record, Error))
        return No(
            TEXT("This is the game's own asset. Make a copy of it in your mod's folder to change and export it."));
    // A record naming another package is a copy's, which is the creator's. A record that cannot be read was the game's.
    if (!Error.IsEmpty() && Error != TEXT("original-record-asset-identity-differs"))
        return No(TEXT("Kadan cannot read the game's record of this asset. Import it again from the game."));
    return true;
}

// Internal publication boundary only: a reused object reconstructed from game source must
// not inherit a creator operation performed on its previous contents.
void P4ForgetCreatorSourceDuringGameImport(UObject* Asset)
{
    if (!IsInGameThread() || !IsValid(Asset) || !P4ImportPublication::IsImporting())
        return;
    // The publication boundary forgets creator/unknown evidence, not a held negative
    // marker whose fields and identity belong to an active native fill rollback guard.
    if (!P4HasIntrinsicGameSource(Asset))
        if (IInterface_AssetUserData* Owner = Cast<IInterface_AssetUserData>(Asset))
            Owner->RemoveUserDataOfClass(UP4CreatorSourceData::StaticClass());
    if (UMetaData* Meta = ExistingMetadata(Asset))
        Meta->RemoveValue(Asset, SourceKey);
}

// Called only by the validated explicit instance constructor, after applying its inputs.
// There is deliberately no public API that stamps creator origin onto an arbitrary existing object.
void P4RecordBuiltCreatorInstance(UMaterialInstanceConstant* Instance)
{
    Record(Instance, TEXT("creator-instance"), TEXT("instance-builder"));
}
void P4RecordFactoryCreatedMaterial(UMaterial* Material)
{
    Record(Material, TEXT("creator-material"), TEXT("material-factory"));
}
void P4RecordFactoryCreatedInstance(UMaterialInstanceConstant* Instance)
{
    Record(Instance, TEXT("creator-instance"), TEXT("instance-factory"));
}
void P4InitializeCreatorSource()
{
    if (!GEditor || ImportSubsystem.IsValid())
        return;
    UImportSubsystem* Subsystem = GEditor->GetEditorSubsystem<UImportSubsystem>();
    if (!Subsystem)
        return;
    ImportSubsystem = Subsystem;
    PreHandle = Subsystem->OnAssetPreImport.AddStatic(&BeforeImport);
    PostHandle = Subsystem->OnAssetPostImport.AddStatic(&AfterImport);
}
void P4ShutdownCreatorSource()
{
    if (UImportSubsystem* Subsystem = ImportSubsystem.Get())
    {
        Subsystem->OnAssetPreImport.Remove(PreHandle);
        Subsystem->OnAssetPostImport.Remove(PostHandle);
    }
    Attempts.Reset();
    ImportSubsystem.Reset();
    PreHandle.Reset();
    PostHandle.Reset();
}
