#include "P4KadanReadOnly.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4GameReplacement.h"
#include "P4KadanAssetPolicy.h"
#include "P4KadanTestSeams.h"
#include "P4OriginalImportSave.h"
#include "P4CreatorSource.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4CreatorMaterialFactories.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"
#include "IAssetTools.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Animation/AnimSequence.h"
#include "P4RecoveredParticleSystem.h"
#include "Particles/ParticleSystem.h"
#include "Particles/TypeData/ParticleModuleTypeDataGpu.h"
#include "Distributions/DistributionFloat.h"
#include "Distributions/DistributionVector.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/App.h"
#include "Misc/BlacklistNames.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/GCObject.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"

bool P4RecordKnownGameSource(UObject* Asset, const FString& OriginalPackage, bool Proxy);

namespace P4Kadan
{
void FlushEditGuards();
void InitializeEditGuards();
void ShutdownEditGuards();
namespace
{
bool Installed = false;
#if P4_KADAN_TEST_SEAMS
FP4KadanCopyTestSeams CopyTestSeams;
FString CopyCompletionFailurePackage;
#endif
FDelegateHandle Loaded, Saved, Delete, Tags, Added, Removed, PackageSaved, Modified, PreModified, Ticking;
FCoreUObjectDelegates::FIsPackageOKToSaveDelegate PreviousSave, OurSave;
TMap<FName, FName> Owners;
const FName LockTag(TEXT("P4KadanReadOnly"));
const FName GameScript(TEXT(P4_GAME_SCRIPT_PACKAGE));
TArray<FString> Messages;
// What a locked object was before something modified it, put back on the next tick.
// The held values may be the only references left after consolidate or a graph edit.
// Traverse those buffers during GC so rollback never installs a collected reference.
struct FPropertyValue final : public FGCObject
{
    // The compiler deletes generated-class FProperties even while their UStruct is
    // still alive. Own the descriptor (including container children) used by this
    // buffer; retaining its owner UObject does not retain those original fields.
    TUniquePtr<FProperty> Property;
    UStruct* PropertyOwner;
    const int32 OwnerFieldSerial;
    UObject* OwnerObject;
    void* Value;
    FPropertyValue(FProperty* InProperty, UObject* Object)
        : Property(CastFieldChecked<FProperty>(FField::Duplicate(InProperty, GetTransientPackage()))),
          PropertyOwner(InProperty->GetOwnerStruct()), OwnerFieldSerial(PropertyOwner->FieldPathSerialNumber),
          OwnerObject(Object)
    {
        Value = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
        Property->InitializeValue(Value);
        Property->CopyCompleteValue(Value, InProperty->ContainerPtrToValuePtr<void>(Object));
    }
    ~FPropertyValue()
    {
        Property->DestroyValue(Value);
        FMemory::Free(Value);
    }
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        if (IsGarbageCollecting())
        {
            // A detached duplicate is not in any UStruct's ChildProperties list.
            // Its native collector retains the type objects and nested descriptors
            // that SerializeItem/DestroyValue still require after class purging.
            Collector.AddReferencedObject(PropertyOwner);
            Property->AddReferencedObjects(Collector);
            // The collector itself: report the held value in place. GC keeps what it names alive and
            // clears a reference to an object being destroyed, so a later put back never installs a
            // pointer to freed memory (a retired material named by a game class default, for one).
            Collector.AddReferencedObject(OwnerObject);
            for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
                Property->SerializeItem(
                    FStructuredArchiveFromArchive(Collector.GetVerySlowReferenceCollectorArchive()).GetSlot(),
                    static_cast<uint8*>(Value) + SIZE_T(Index) * Property->ElementSize, nullptr);
            return;
        }
        UObject* HeldOwner = OwnerObject;
        Collector.AddReferencedObject(HeldOwner);
        // Reference-replacement archives also traverse FGCObject references. Enumerate a temporary
        // property copy so a consolidate operation cannot rewrite the baseline we must restore.
        TUniquePtr<FProperty> TraversedProperty(
            CastFieldChecked<FProperty>(FField::Duplicate(Property.Get(), GetTransientPackage())));
        TraversedProperty->AddReferencedObjects(Collector);
        void* Traversed = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
        Property->InitializeValue(Traversed);
        Property->CopyCompleteValue(Traversed, Value);
        for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
            Property->SerializeItem(
                FStructuredArchiveFromArchive(Collector.GetVerySlowReferenceCollectorArchive()).GetSlot(),
                static_cast<uint8*>(Traversed) + SIZE_T(Index) * Property->ElementSize, nullptr);
        Property->DestroyValue(Traversed);
        FMemory::Free(Traversed);
    }
    virtual FString GetReferencerName() const override { return TEXT("Kadan held read-only properties"); }
    FProperty* CurrentProperty(UObject* Object) const
    {
        // Never address a current instance through a captured offset from a
        // regenerated layout, even if the compiler reused the class UObject/name.
        if (!IsValid(Object) || !IsValid(PropertyOwner) || PropertyOwner->FieldPathSerialNumber != OwnerFieldSerial)
            return nullptr;
        FProperty* Current = FindFProperty<FProperty>(Object->GetClass(), Property->GetFName());
        if (!Current || Current->GetOwnerStruct() != PropertyOwner || !Property->SameType(Current) ||
            Current->ArrayDim != Property->ArrayDim || Current->ElementSize != Property->ElementSize ||
            Current->GetOffset_ForInternal() != Property->GetOffset_ForInternal() ||
            Current->GetMinAlignment() != Property->GetMinAlignment() ||
            Current->GetPropertyFlags() != Property->GetPropertyFlags() || Current->GetOffset_ForInternal() < 0 ||
            int64(Current->GetOffset_ForInternal()) + Current->GetSize() > Object->GetClass()->GetPropertiesSize())
            return nullptr;
        const FBoolProperty* BeforeBool = CastField<FBoolProperty>(Property.Get());
        const FBoolProperty* CurrentBool = CastField<FBoolProperty>(Current);
        if (BeforeBool)
        {
            // UE4.26 exposes no bit-mask accessors. Its public setter reveals both
            // the bit written and bits cleared without reading any real object.
            if (!CurrentBool || BeforeBool->IsNativeBool() != CurrentBool->IsNativeBool())
                return nullptr;
            uint64 BeforeTrue = 0, CurrentTrue = 0, BeforeFalse = ~uint64(0), CurrentFalse = ~uint64(0);
            BeforeBool->SetPropertyValue(&BeforeTrue, true);
            CurrentBool->SetPropertyValue(&CurrentTrue, true);
            BeforeBool->SetPropertyValue(&BeforeFalse, false);
            CurrentBool->SetPropertyValue(&CurrentFalse, false);
            if (BeforeTrue != CurrentTrue || BeforeFalse != CurrentFalse)
                return nullptr;
        }
        return Current;
    }
    bool Changed(UObject* Object) const
    {
        const FProperty* Current = CurrentProperty(Object);
        if (!Current)
            return false;
        // Identical compares one element: a fixed array (UMaterial::CustomizedUVs[8], say) is compared element by element.
        for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
            if (!Property->Identical(static_cast<const uint8*>(Value) + SIZE_T(Index) * Property->ElementSize,
                    Current->ContainerPtrToValuePtr<void>(Object, Index), PPF_None))
                return true;
        return false;
    }
    void Restore(UObject* Object) const
    {
        if (const FProperty* Current = CurrentProperty(Object))
            Property->CopyCompleteValue(Current->ContainerPtrToValuePtr<void>(Object), Value);
    }
};
struct FSnapshot
{
    TWeakObjectPtr<UObject> Object;
    TArray<TSharedPtr<FPropertyValue>> Values;
    EObjectFlags Flags;
    uint64 Frame;
};
TArray<FSnapshot> Snapshots;
// Loading a large Blueprint emits many Modify calls. Membership must not scan every held
// object's weak pointer for every part of the package. Keep the rollback values unchanged.
TSet<TWeakObjectPtr<UObject>> SnapshotObjects;
void RemoveSnapshots(TFunctionRef<bool(const FSnapshot&)> Select)
{
    Snapshots.RemoveAll(
        [&](const FSnapshot& One)
        {
            if (!Select(One))
                return false;
            SnapshotObjects.Remove(One.Object);
            return true;
        });
}
bool Restoring = false, Capturing = false;
// Files of locked packages the importer made writable to write them again.
TSet<FString> Unprotected;
// Editor property notifications on a recovered mesh must not compile missing source geometry.
// Use the engine's existing cooked-editor cache path only for that notification, then restore it.
TMap<TWeakObjectPtr<UPackage>, bool> MeshNotifications;

// The keys Kadan's import keeps with a game asset to say where it came from.
const TCHAR* const ProvenanceKeys[] = {TEXT("P4OriginalRecord"), TEXT("P4NativeTextureBaseline"),
    TEXT("P4NativeMeshBaseline"), TEXT("P4NativeObjectBaseline"), TEXT("P4NativeObjectValuesNotHeld"),
    TEXT("P4NativeLevelBaseline"), TEXT("P4NativeLevelImported"), TEXT("P4NativeLevelLighting"),
    TEXT("P4NativeLevelRunLines"), TEXT("P4GameRows"), TEXT("P4MaterialInstanceNativeSourceV1")};

FString FileOf(FName Package)
{
    FString File;
    if (!FPackageName::DoesPackageExist(Package.ToString(), nullptr, &File))
        return FString();
    return FPaths::ConvertRelativePathToFull(File);
}

void SetFileReadOnly(const FString& File, bool ReadOnly)
{
    if (File.IsEmpty())
        return;
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (Files.FileExists(*File) && Files.IsReadOnly(*File) != ReadOnly)
        Files.SetReadOnly(*File, ReadOnly);
}

void SetPackageLock(FName Package, bool Locked)
{
    auto& Filter =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().GetWritableFolderBlacklist();
    if (Locked && !Owners.Contains(Package))
    {
        const FName Owner(*(TEXT("Kadan.ReadOnly:") + Package.ToString()));
        Filter->AddBlacklistItem(Owner, Package.ToString());
        Owners.Add(Package, Owner);
        // Unreal's own save refuses a read-only file, whatever asks for the save.
        SetFileReadOnly(FileOf(Package), true);
    }
    else if (!Locked && Owners.Contains(Package))
    {
        Filter->UnregisterOwner(Owners.FindChecked(Package));
        Owners.Remove(Package);
        RemoveSnapshots([Package](const FSnapshot& One)
            { return !One.Object.IsValid() || One.Object->GetOutermost()->GetFName() == Package; });
        SetFileReadOnly(FileOf(Package), false);
    }
}

bool HoldsLockedAsset(UPackage* Package)
{
    if (!Package)
        return false;
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, false);
    for (UObject* Object : Objects)
        if (Object && Object->IsAsset() && !GetAssetPolicy(Object).AllowEdit)
            return true;
    return false;
}

bool PackageIsLocked(UPackage* Package)
{
    return Package && (Owners.Contains(Package->GetFName()) || HoldsLockedAsset(Package));
}

FString AssetName(const UObject* Object)
{
    return FPackageName::GetLongPackageAssetName(Object->GetOutermost()->GetName());
}

// Package-copy paths can carry the game's source keys, naming the old package. Keep those
// keys as copy provenance rather than an original record with the wrong identity. Ordinary
// engine DuplicateAsset does not copy this metadata; positive creator-source qualification
// at export must also cover that path.
bool ForgetCopiedSource(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    UMetaData* Meta = Package->GetMetaData();
    bool Copy = false;
    for (const TCHAR* Key : ProvenanceKeys)
    {
        const FString Text = Meta->GetValue(Asset, Key);
        TSharedPtr<FJsonObject> Json;
        FString Named;
        if (!Text.IsEmpty() && P4ParseJson(Text, Json) && Json->TryGetStringField(TEXT("package"), Named) &&
            !Named.IsEmpty() && Named != Package->GetName())
            Copy = true;
    }
    if (!Copy)
        return false;
    TSharedPtr<FJsonObject> Source = MakeShared<FJsonObject>();
    Source->SetNumberField(TEXT("format"), 1);
    Source->SetStringField(TEXT("kind"), TEXT("copied-game-asset"));
    TSharedPtr<FJsonObject> Records = MakeShared<FJsonObject>();
    for (const TCHAR* Key : ProvenanceKeys)
    {
        const FString Value = Meta->GetValue(Asset, Key);
        // Preserve literal custody presence, including malformed empty text. A
        // copied history is evidence, never fresh original import authority.
        if (!Value.IsEmpty() ||
            (FCString::Strcmp(Key, TEXT("P4MaterialInstanceNativeSourceV1")) == 0 && Meta->HasValue(Asset, Key)))
            Records->SetStringField(Key, Value);
    }
    Source->SetObjectField(TEXT("source_records"), Records);
    FString Copied;
    FJsonSerializer::Serialize(Source.ToSharedRef(), TJsonWriterFactory<>::Create(&Copied));
    Meta->SetValue(Asset, TEXT("P4KadanCopiedGameAsset"), *Copied);
    for (const TCHAR* Key : ProvenanceKeys)
        Meta->RemoveValue(Asset, Key);
    Package->SetDirtyFlag(true);
    UE_LOG(LogTemp, Display,
        TEXT("Kadan: %s is a copy that still contains the game's data. Copying it does not make it creator source."),
        *Package->GetName());
    return true;
}

// The game's values this engine has no place for (P4StockTags.h) and the native data the game's classes
// write after their properties (P4GameNativeTails.h) are kept as editor-only package metadata on the
// original's objects. Unreal's Duplicate copies the objects without their package's metadata, so a copy
// lost them and its cook was no longer the game's. A copy made from a loaded game original gets them once,
// from that original, with the original's own objects named as the copy's.
// A particle system's native custody (P4RecoveredParticleSystem.h) goes with them when the original's is bound.
const TCHAR* const CarriedKeys[] = {
    TEXT("P4GameStockTags"), TEXT("P4GameNativeTail"), TEXT("P4OriginalParticleCutoutCustody")};

// Text with every whole reference to From (the path itself, or a path inside it) naming To instead.
FString Renamed(const FString& Text, const FString& From, const FString& To)
{
    FString Out;
    int32 At = 0;
    for (int32 Found; (Found = Text.Find(From, ESearchCase::CaseSensitive, ESearchDir::FromStart, At)) != INDEX_NONE;)
    {
        const int32 End = Found + From.Len();
        const TCHAR Next = End < Text.Len() ? Text[End] : TCHAR(0);
        const bool Whole = !FChar::IsIdentifier(Next) && Next != TCHAR('/');
        Out += Text.Mid(At, Found - At) + (Whole ? To : From);
        At = End;
    }
    return Out + Text.Mid(At);
}

// A loaded game original: its record names its own package.
bool LoadedOriginal(UObject* Object)
{
    FP4OriginalRecord Record;
    FString Error;
    return Object && P4IsContentPackageIgnoringCase(Object->GetOutermost()->GetName()) &&
        P4ReadOriginalRecord(Object, Record, Error) && Record.Package == Object->GetOutermost()->GetName();
}

bool HasCarriedData(UObject* Asset)
{
    UMetaData* Meta = Asset->GetOutermost()->GetMetaData();
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Asset, Objects, true);
    Objects.Add(Asset);
    for (UObject* Object : Objects)
        for (const TCHAR* Key : CarriedKeys)
            if (Meta->HasValue(Object, Key))
                return true;
    return false;
}

// The reflected values of an object and of everything inside it, by place below the asset, with the
// asset's own path read as As.
TArray<FString> Content(UObject* Asset, const FString& As)
{
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Asset, Objects, true);
    Objects.Add(Asset);
    const FString Path = Asset->GetPathName();
    TArray<FString> Out;
    for (UObject* Object : Objects)
    {
        const FString Place = Object->GetClass()->GetPathName() + TEXT(" ") + Renamed(Object->GetPathName(), Path, As);
        for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
        {
            if (It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient))
                continue;
            // What a particle system builds from its modules (a GPU emitter's data, the baked tables of its
            // distributions) is the engine's on a copy and the game's on the original: what it is built from is compared.
            if (Object->IsA<UParticleModuleTypeDataGpu>() &&
                (It->GetFName() == TEXT("ResourceData") || It->GetFName() == TEXT("EmitterInfo")))
                continue;
            if (Object->IsA<UDistribution>() && It->GetFName() == TEXT("bBakedDataSuccesfully"))
                continue;
            const FStructProperty* Struct = CastField<FStructProperty>(*It);
            const FProperty* Source = Struct &&
                    (Struct->Struct == FRawDistributionFloat::StaticStruct() ||
                        Struct->Struct == FRawDistributionVector::StaticStruct())
                ? Struct->Struct->FindPropertyByName(TEXT("Distribution"))
                : nullptr;
            for (int32 Index = 0; Index < It->ArrayDim; ++Index)
            {
                FString Value;
                const void* At = It->ContainerPtrToValuePtr<void>(Object, Index);
                if (Source)
                    Source->ExportTextItem(Value, Source->ContainerPtrToValuePtr<void>(At), nullptr, Object, PPF_None);
                else
                    It->ExportTextItem(Value, At, nullptr, Object, PPF_None);
                Out.Add(Place + TEXT(" ") + It->GetName() + TEXT(" ") + Renamed(Value, Path, As));
            }
        }
    }
    Out.Sort();
    return Out;
}

// Fingerprint every carried key, including its object/class and literal presence. The copy's own
// lineage is separate from these values; changing/removing any payload invalidates it.
bool CarriedDigest(UObject* Asset, FString& Digest)
{
    UMetaData* Meta = Asset->GetOutermost()->GetMetaData();
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Asset, Objects, true);
    Objects.Add(Asset);
    TArray<FString> Rows;
    const auto Part = [](const FString& Text) { return FString::FromInt(Text.Len()) + TEXT(":") + Text; };
    for (UObject* Object : Objects)
        for (const TCHAR* Key : CarriedKeys)
            if (Meta->HasValue(Object, Key))
                Rows.Add(Part(Object->GetPathName(Asset)) + Part(Object->GetClass()->GetPathName()) + Part(Key) +
                    Part(Meta->GetValue(Object, Key)));
    if (!Rows.Num())
        return false;
    if (Meta->HasValue(Asset, TEXT("P4CopiedParticleCustodyFrom")))
        Rows.Add(Part(TEXT("P4CopiedParticleCustodyFrom")) +
            Part(Meta->GetValue(Asset, TEXT("P4CopiedParticleCustodyFrom"))));
    Rows.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
    FString Text;
    for (const FString& Row : Rows)
        Text += Part(Row);
    return P4HashUtf8(Text, Digest);
}

// A saved copy can supply its values after the original has unloaded. Mere metadata presence is
// insufficient: require the copy's complete payload seal, its own identity, and retained original
// identity. This is copy history, never authority for an original import or creator-source admission.
bool CopyDonor(UObject* Asset, FString& Origin, FString& OriginDigest, TSharedPtr<FJsonObject>& Records)
{
    UMetaData* Meta = Asset->GetOutermost()->GetMetaData();
    if (Meta->HasValue(Asset, TEXT("P4KadanCopiedGameDataFailure")))
        return false;
    if (LoadedOriginal(Asset))
    {
        Origin = Asset->GetOutermost()->GetName();
        Records = MakeShared<FJsonObject>();
        for (const TCHAR* Key : ProvenanceKeys)
            if (Meta->HasValue(Asset, Key))
                Records->SetStringField(Key, Meta->GetValue(Asset, Key));
        return CarriedDigest(Asset, OriginDigest);
    }
    TSharedPtr<FJsonObject> Lineage;
    double Format = 0;
    FString Kind, Package, Class, Expected, Actual;
    const TSharedPtr<FJsonObject>* Source = nullptr;
    if (!P4ParseJson(Meta->GetValue(Asset, TEXT("P4KadanCopiedGameAsset")), Lineage) || Lineage->Values.Num() != 8 ||
        !Lineage->TryGetNumberField(TEXT("format"), Format) || Format != 1 ||
        !Lineage->TryGetStringField(TEXT("kind"), Kind) || Kind != TEXT("copied-game-asset") ||
        !Lineage->TryGetStringField(TEXT("package"), Package) ||
        !P4ExactNativeString(Package, Asset->GetOutermost()->GetName()) ||
        !Lineage->TryGetStringField(TEXT("class"), Class) ||
        !P4ExactNativeString(Class, Asset->GetClass()->GetPathName()) ||
        !Lineage->TryGetStringField(TEXT("origin_package"), Origin) || !P4IsValidContentPackageIgnoringCase(Origin) ||
        Origin == Package || !Lineage->TryGetStringField(TEXT("origin_data_sha256"), OriginDigest) ||
        OriginDigest.Len() != 64 || !Lineage->TryGetStringField(TEXT("carried_data_sha256"), Expected) ||
        !CarriedDigest(Asset, Actual) || !P4ExactNativeString(Expected, Actual) ||
        !Lineage->TryGetObjectField(TEXT("source_records"), Source))
        return false;
    for (TCHAR C : OriginDigest)
        if (!(C >= TEXT('0') && C <= TEXT('9')) && !(C >= TEXT('a') && C <= TEXT('f')))
            return false;
    // Native copies retain the full original record. Legacy copies lacking this seal are not guessed
    // to be donors: make a new copy from the original, rather than silently dropping their values.
    FString OriginalText, OriginalPackage, OriginalClass, Owner;
    double OriginalFormat = 0;
    TSharedPtr<FJsonObject> Original;
    if (!(*Source)->TryGetStringField(TEXT("P4OriginalRecord"), OriginalText) || !P4ParseJson(OriginalText, Original) ||
        !Original->TryGetNumberField(TEXT("format"), OriginalFormat) || OriginalFormat != 2 ||
        !Original->TryGetStringField(TEXT("owner"), Owner) || Owner != TEXT("p4mapkit-original") ||
        !Original->TryGetStringField(TEXT("package"), OriginalPackage) ||
        !P4ExactNativeString(OriginalPackage, Origin) || !Original->TryGetStringField(TEXT("class"), OriginalClass) ||
        !P4ExactNativeString(OriginalClass, Class))
        return false;
    for (const auto& Pair : (*Source)->Values)
    {
        FString Value;
        bool Known = false;
        for (const TCHAR* Key : ProvenanceKeys)
            if (Pair.Key == Key)
                Known = true;
        if (!Known || !Pair.Value->TryGetString(Value))
            return false;
    }
    FString Intrinsic;
    if (P4HasIntrinsicGameSource(Asset, &Intrinsic) && !P4ExactNativeString(Intrinsic, Origin))
        return false;
    Records = *Source;
    return true;
}

// UE4's AssetCreated event has no source argument. Intrinsic origin narrows the source family where
// available; otherwise require one exact, loaded original or sealed copy. Never choose between two
// indistinguishable donors with different unreflected values, or an unsealed legacy copy. The initial
// payload seal joins equivalent copies even when native GPU object references were rebound per copy.
UObject* CopiedFrom(UObject* Asset, FString& Origin, FString& OriginDigest, TSharedPtr<FJsonObject>& Records)
{
    UPackage* Package = Asset->GetOutermost();
    if (LoadedOriginal(Asset) || HasCarriedData(Asset))
        return nullptr;
    FString Intrinsic;
    P4HasIntrinsicGameSource(Asset, &Intrinsic);
    // Source control may save a duplicate before publishing AssetCreated. Preserve the intrinsic
    // source route in that case; metadata-only discovery remains limited to newly made packages.
    if (Intrinsic.IsEmpty() && (!Package->IsDirty() || FPackageName::DoesPackageExist(Package->GetName())))
        return nullptr;
    TArray<UObject*> Candidates;
    GetObjectsOfClass(Asset->GetClass(), Candidates, false);
    UObject* Found = nullptr;
    bool Unsealed = false;
    const TArray<FString> Mine = Content(Asset, Asset->GetPathName());
    for (UObject* Candidate : Candidates)
    {
        if (Candidate == Asset || !Candidate->IsAsset() || !HasCarriedData(Candidate))
            continue;
        FString CandidateOrigin, CandidateDigest;
        TSharedPtr<FJsonObject> CandidateRecords;
        const bool Qualified = CopyDonor(Candidate, CandidateOrigin, CandidateDigest, CandidateRecords);
        if (Qualified && !Intrinsic.IsEmpty() && !P4ExactNativeString(Intrinsic, CandidateOrigin))
            continue;
        const TArray<FString> Theirs = Content(Candidate, Asset->GetPathName());
        // An original can lack unused default distribution objects made by Unreal's Duplicate. A
        // copy donor already contains those objects, so its complete content must match exactly.
        bool Matches = Mine.Num() == Theirs.Num();
        for (int32 Index = 0; Matches && Index < Mine.Num(); ++Index)
            Matches = P4ExactNativeString(Mine[Index], Theirs[Index]);
        if (!Matches && LoadedOriginal(Candidate))
        {
            Matches = !Theirs.FindByPredicate(
                [&](const FString& Line)
                {
                    return !Mine.ContainsByPredicate(
                        [&](const FString& Held) { return P4ExactNativeString(Line, Held); });
                });
        }
        if (!Matches)
            continue;
        if (!Qualified)
        {
            Unsealed = true;
            continue;
        }
        if (Found)
        {
            bool Equivalent = P4ExactNativeString(Origin, CandidateOrigin) &&
                P4ExactNativeString(OriginDigest, CandidateDigest);
            for (const TCHAR* Key : ProvenanceKeys)
            {
                FString Previous, Next;
                const bool Had = Records->TryGetStringField(Key, Previous),
                           Has = CandidateRecords->TryGetStringField(Key, Next);
                if (Had != Has || (Had && !P4ExactNativeString(Previous, Next)))
                    Equivalent = false;
            }
            if (Equivalent)
                continue;
            UE_LOG(LogTemp, Warning,
                TEXT(
                    "Kadan: the saved game values for %s cannot be verified: matching sources hold different values. Close the other source and duplicate again."),
                *Asset->GetPathName());
            return nullptr;
        }
        Found = Candidate;
        Origin = CandidateOrigin;
        OriginDigest = CandidateDigest;
        Records = CandidateRecords;
    }
    if (Unsealed)
    {
        UE_LOG(LogTemp, Warning,
            TEXT(
                "Kadan: the saved game values for %s cannot be verified. Make a fresh copy from the game original before duplicating this older or changed copy."),
            *Asset->GetPathName());
        return nullptr;
    }
    return Found;
}

bool CarryCopiedGameData(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    FString Origin, OriginDigest;
    TSharedPtr<FJsonObject> Records;
    UObject* Original = CopiedFrom(Asset, Origin, OriginDigest, Records);
    if (!Original)
        return false;
    UPackage* Source = Original->GetOutermost();
    const FString From = Source->GetName();
    UMetaData* Meta = Package->GetMetaData();
    UMetaData* SourceMeta = Source->GetMetaData();
    // Every incomplete exit is an explicit export refusal, including a native refresh that already
    // touched derived state. Only complete validation below clears this mark.
    Meta->SetValue(
        Asset, TEXT("P4KadanCopiedGameDataFailure"), TEXT("The saved game values could not be completely copied."));
    TArray<UObject*> Theirs;
    GetObjectsWithOuter(Original, Theirs, true);
    Theirs.Add(Original);
    struct FCarry
    {
        UObject* Object;
        const TCHAR* Key;
        FString Text;
    };
    TArray<FCarry> Carry;
    const FString OldPath = Original->GetPathName(), NewPath = Asset->GetPathName();
    for (UObject* Object : Theirs)
    {
        TArray<const TCHAR*> Keys;
        for (const TCHAR* Key : CarriedKeys)
            if (SourceMeta->HasValue(Object, Key))
                Keys.Add(Key);
        if (!Keys.Num())
            continue;
        TArray<FName> Chain;
        for (UObject* At = Object; At != Original; At = At->GetOuter())
            Chain.Add(At->GetFName());
        UObject* Target = Asset;
        for (int32 Index = Chain.Num() - 1; Index >= 0 && Target; --Index)
            Target = StaticFindObjectFast(UObject::StaticClass(), Target, Chain[Index]);
        if (!Target || Target->GetClass() != Object->GetClass())
        {
            UE_LOG(LogTemp, Warning, TEXT("Kadan: %s has no matching %s; its kept game values could not be copied."),
                *Package->GetName(), *Object->GetPathName(Original));
            return false;
        }
        if (Keys.Contains(CarriedKeys[2]))
        {
            TArray<FString> Bound;
            FString Why;
            UParticleSystem* System = Cast<UParticleSystem>(Original);
            const bool BoundOriginal = LoadedOriginal(Original) && System &&
                P4OriginalParticleCutoutCacheKeys(System, Bound, Why);
            // CopyDonor already checked the exact metadata seal. The ordinary copied-custody parser,
            // module/input guards and native restoration must also succeed; a marker alone never suffices.
            const bool BoundCopy = !LoadedOriginal(Original) && System && P4IsCopiedParticleCustody(System) &&
                P4PrepareOriginalParticleState(System, Why);
            if (Object != Original || (!BoundOriginal && !BoundCopy))
            {
                UE_LOG(LogTemp, Warning, TEXT("Kadan: %s cannot carry the native particle data of %s: %s"),
                    *Package->GetName(), *From, *Why);
                return false;
            }
        }
        for (const TCHAR* Key : Keys)
        {
            FString Text = Renamed(SourceMeta->GetValue(Object, Key), OldPath, NewPath);
            Text.ReplaceInline(*(TEXT("\"package\":\"") + From + TEXT("\"")),
                *(TEXT("\"package\":\"") + Package->GetName() + TEXT("\"")), ESearchCase::CaseSensitive);
            Text.ReplaceInline(*(TEXT("\"object\":\"") + Original->GetName() + TEXT(".")),
                *(TEXT("\"object\":\"") + Asset->GetName() + TEXT(".")), ESearchCase::CaseSensitive);
            Carry.Add({Target, Key, MoveTemp(Text)});
        }
    }
    if (!Carry.Num())
        return false;
    TSharedPtr<FJsonObject> Lineage = MakeShared<FJsonObject>();
    Lineage->SetNumberField(TEXT("format"), 1);
    Lineage->SetStringField(TEXT("kind"), TEXT("copied-game-asset"));
    Lineage->SetObjectField(TEXT("source_records"), Records);
    Lineage->SetStringField(TEXT("package"), Package->GetName());
    Lineage->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
    Lineage->SetStringField(TEXT("origin_package"), Origin);
    Lineage->SetStringField(TEXT("origin_data_sha256"), OriginDigest);
    const auto PublishHistory = [&]()
    {
        FString Text;
        auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
        if (!FJsonSerializer::Serialize(Lineage.ToSharedRef(), Writer))
            return false;
        Meta->SetValue(Asset, TEXT("P4KadanCopiedGameAsset"), *Text);
        return true;
    };
    // Before the native refresh, preserve history without a donor seal. If a later GPU/cache refresh
    // fails after touching derived state, keep the complete custody so ordinary cook preparation still
    // validates/restores it. Removing it would let a partially rebuilt asset silently cook as ordinary data.
    if (!PublishHistory())
        return false;
    for (const FCarry& One : Carry)
        Meta->SetValue(One.Object, One.Key, *One.Text);
    if (SourceMeta->HasValue(Original, CarriedKeys[2]))
        Meta->SetValue(Asset, TEXT("P4CopiedParticleCustodyFrom"), *Origin);
    bool Completed = true;
    if (UParticleSystem* System = Cast<UParticleSystem>(Asset))
        if (P4IsCopiedParticleCustody(System))
        {
            int32 Restored = 0;
            FString Why;
            Completed = P4RebindCopiedParticleCustody(Cast<UParticleSystem>(Original), System, Why) &&
                P4RestoreOriginalParticleCutouts(System, Restored, Why);
            if (!Completed)
                UE_LOG(LogTemp, Warning, TEXT("Kadan: %s could not take the native particle data of %s: %s"),
                    *Package->GetName(), *From, *Why);
        }
#if P4_KADAN_TEST_SEAMS
    if (P4ExactNativeString(CopyCompletionFailurePackage, Package->GetName()))
    {
        CopyCompletionFailurePackage.Reset();
        Completed = false;
    }
#endif
    FString Digest;
    if (!Completed || !CarriedDigest(Asset, Digest))
    {
        UE_LOG(LogTemp, Warning,
            TEXT(
                "Kadan: the saved game values for %s could not be verified. Its values and history were kept for validation; make a new copy before using it."),
            *Package->GetName());
        return false;
    }
    Lineage->SetStringField(TEXT("carried_data_sha256"), Digest);
    if (!PublishHistory())
        return false;
    Meta->RemoveValue(Asset, TEXT("P4KadanCopiedGameDataFailure"));
    // Only a newly made copy takes this flag; later load/save callbacks must not clear a creator's edits.
    if (UAnimSequence* Sequence = Cast<UAnimSequence>(Asset))
        if (FBoolProperty* Rebake = FindFProperty<FBoolProperty>(UAnimSequence::StaticClass(), TEXT("bNeedsRebake")))
            Rebake->SetPropertyValue_InContainer(Sequence, Rebake->GetPropertyValue_InContainer(Original));
    UE_LOG(LogTemp, Display, TEXT("Kadan: %s keeps %d game-value records copied from %s."), *Package->GetName(),
        Carry.Num(), *From);
    return true;
}

void RegistryAsset(const FAssetData& Asset)
{
    if (!P4IsContentPackageIgnoringCase(Asset.PackageName.ToString()))
        return;
    // A Duplicate of a game original: the game's values its package metadata held.
    if (Asset.IsAssetLoaded() && !P4ImportPublication::IsImporting())
        CarryCopiedGameData(Asset.GetAsset());
    // Preserve source evidence for package copies that retained original metadata.
    if (Asset.IsAssetLoaded() && !P4ImportPublication::IsImporting() && ForgetCopiedSource(Asset.GetAsset()))
    {
        UpdateReadOnlyAsset(Asset.GetAsset());
        return;
    }
    FString Locked;
    if (Asset.GetTagValue(LockTag, Locked))
        SetPackageLock(Asset.PackageName, Locked == TEXT("1"));
    else if (Asset.IsAssetLoaded())
    {
        // Older packages may store provenance only in UObject metadata. Classify
        // an already loaded object, never materialize every tagless package during
        // registry discovery. Real asset loading still invokes UpdateReadOnlyAsset
        // before edit/delete/save routes; missing tags never qualify creator source.
        UpdateReadOnlyAsset(Asset.GetAsset());
    }
}

// Blueprint compilation on load reconstructs graphs and replaces class property layouts after
// PostLoad itself has returned. Such initialization cannot provide a stable rollback baseline.
bool RegeneratingBlueprintOnLoad(UObject* Object)
{
    for (UObject* Owner = Object; Owner; Owner = Owner->GetOuter())
    {
        if (const UBlueprint* Blueprint = Cast<UBlueprint>(Owner))
            if (Blueprint->bIsRegeneratingOnLoad)
                return true;
        if (const UClass* Class = Cast<UClass>(Owner))
            if (const UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
                if (Blueprint->bIsRegeneratingOnLoad)
                    return true;
        if (const UBlueprint* Blueprint = Cast<UBlueprint>(Owner->GetClass()->ClassGeneratedBy))
            if (Blueprint->bIsRegeneratingOnLoad)
                return true;
    }
    return false;
}

void Snapshot(UObject* Object)
{
    if (Restoring || Capturing || !Installed || !IsInGameThread() || !Object || P4ImportPublication::IsImporting())
        return;
    // PostLoad and construction notify the engine while their saved fields are still being populated.
    // Those are initialization rather than creator edits, and cannot supply a rollback baseline.
    const auto& Context = FUObjectThreadContext::Get();
    if (GIsEditorLoadingPackage || Context.IsRoutingPostLoad || Context.IsInConstructor || IsGarbageCollecting() ||
        Object->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_NeedInitialization))
        return;
    TGuardValue<bool> CaptureGuard(Capturing, true);
    if (!P4ImportPublication::IsSaving(Object->GetOutermost()) && !RegeneratingBlueprintOnLoad(Object))
    {
        P4RecordGameBlueprintEdit(Object);
        P4RecordGameMaterialInstanceEdit(Object);
    }
    if (!IsLockedObject(Object) || P4ImportPublication::IsSaving(Object->GetOutermost()))
        return;
    if (RegeneratingBlueprintOnLoad(Object))
        return;
    // A replacement can notify only a material expression while changing its owning material too.
    // Hold the package's properties before any one object changes.
    if (UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
        if (Mesh->GetNumSourceModels() > 0 && !Mesh->IsMeshDescriptionValid(0))
        {
            UPackage* Package = Mesh->GetOutermost();
            if (!MeshNotifications.Contains(Package))
                MeshNotifications.Add(Package, Package->bIsCookedForEditor);
            Package->bIsCookedForEditor = true;
        }
    TArray<UObject*> Objects;
    GetObjectsWithOuter(
        Object->GetOutermost()->GetFName() == GameScript ? Object : Object->GetOutermost(), Objects, true);
    Objects.AddUnique(Object);
    for (UObject* Part : Objects)
    {
        if (!IsValid(Part) || Part->IsA<UField>() ||
            Part->HasAnyFlags(
                RF_Transient | RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_NeedInitialization) ||
            RegeneratingBlueprintOnLoad(Part))
            continue;
        if (SnapshotObjects.Contains(Part))
            continue;
        FSnapshot Held;
        Held.Object = Part;
        Held.Flags = Part->GetFlags();
        Held.Frame = GFrameCounter;
        for (TFieldIterator<FProperty> It(Part->GetClass()); It; ++It)
            if (!It->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
                Held.Values.Add(MakeShared<FPropertyValue>(*It, Part));
        Snapshots.Add(MoveTemp(Held));
        SnapshotObjects.Add(Part);
    }
}

void PutBack()
{
    if (!Snapshots.Num())
        return;
    // A compiler/reinstance may have replaced an object's field layout since
    // Modify. Such derived values have no address-compatible rollback target.
    // Native graph/asset properties retain their own eligible snapshots.
    RemoveSnapshots(
        [](const FSnapshot& One)
        {
            UObject* Object = One.Object.Get();
            if (!IsValid(Object))
                return true;
            for (const auto& Value : One.Values)
                if (!Value->CurrentProperty(Object))
                {
                    UE_LOG(LogTemp, Display,
                        TEXT("Kadan: retiring the read-only property snapshot for %s after its class layout changed."),
                        *Object->GetFullName());
                    return true;
                }
            return false;
        });
    // Modify broadcasts once per object per engine frame. Keep that frame's baseline if
    // another caller explicitly flushes between two edits in the same frame.
    TArray<FSnapshot>& Held = Snapshots;
    TGuardValue<bool> Guard(Restoring, true);
    // Each package once: its changed objects get their own data back, then the asset is told it changed so
    // that what it works out from its data (a texture's resource, a mesh's render data, a material's shaders)
    // is made again. Parts inside an asset (a material's nodes) are not told on their own.
    TMap<UPackage*, TArray<FSnapshot*>> Packages;
    for (FSnapshot& One : Held)
        if (UObject* Object = One.Object.Get())
            if (IsValid(Object))
                Packages.FindOrAdd(Object->GetOutermost()).Add(&One);
    for (auto& Pair : Packages)
    {
        if (P4ImportPublication::IsImporting())
            break;
        UPackage* Package = Pair.Key;
        TArray<FSnapshot*> Changed;
        for (FSnapshot* One : Pair.Value)
        {
            bool Different = (One->Object->GetFlags() & (RF_Public | RF_Standalone)) !=
                (One->Flags & (RF_Public | RF_Standalone));
            for (const auto& Value : One->Values)
                Different |= Value->Changed(One->Object.Get());
            if (Different)
                Changed.Add(One);
        }
        bool Classes = false;
        for (FSnapshot* One : Pair.Value)
            if (UBlueprint* Blueprint = Cast<UBlueprint>(One->Object.Get()))
                for (UClass* Class : {Blueprint->GeneratedClass.Get(), Blueprint->SkeletonGeneratedClass.Get()})
                    if (Class && Class->GetOutermost() == Package && Class->ClassGeneratedBy != Blueprint)
                    {
                        UE_LOG(
                            LogTemp, Display, TEXT("Kadan: putting back the Blueprint of %s"), *Class->GetFullName());
                        Class->ClassGeneratedBy = Blueprint;
                        Classes = true;
                    }
        if (Classes && !Changed.Num())
        {
            if (!FileOf(Package->GetFName()).IsEmpty())
                Package->SetDirtyFlag(false);
            Tell(FString::Printf(TEXT("Kadan put back %s because this change is not supported for the game asset."),
                *FPackageName::GetLongPackageAssetName(Package->GetName())));
            continue;
        }
        if (!Changed.Num())
        {
            if (!FileOf(Package->GetFName()).IsEmpty())
                Package->SetDirtyFlag(false);
            continue;
        }
        for (FSnapshot* One : Changed)
        {
            UE_LOG(LogTemp, Display, TEXT("Kadan: putting back %s"), *One->Object->GetFullName());
            for (const auto& Value : One->Values)
                if (Value->Changed(One->Object.Get()))
                    Value->Restore(One->Object.Get());
            One->Object->ClearFlags(RF_Public | RF_Standalone);
            One->Object->SetFlags(One->Flags & (RF_Public | RF_Standalone));
        }
        UObject* Asset = nullptr;
        for (FSnapshot* One : Changed)
        {
            UObject* Object = One->Object.Get();
            UObject* Top = Object;
            while (Top->GetOuter() && Top->GetOuter() != Package)
                Top = Top->GetOuter();
            if (Top->IsAsset())
            {
                Asset = Top;
                break;
            }
        }
        // Recovered meshes carry render data rather than source mesh descriptions. Rebuilding them
        // would invoke the engine's unsupported source-mesh path.
        if (Asset && !Asset->IsA<UBlueprint>() && !Asset->IsA<UStaticMesh>())
            Asset->PostEditChange();
        // What an asset works out again can differ from what it had (an id it makes anew), so the data is
        // read back once more after that.
        for (FSnapshot* One : Pair.Value)
            for (const auto& Value : One->Values)
                if (Value->Changed(One->Object.Get()))
                    Value->Restore(One->Object.Get());
        if (!FileOf(Package->GetFName()).IsEmpty())
            Package->SetDirtyFlag(false);
        Tell(FString::Printf(TEXT("Kadan put back %s because this change is not supported for the game asset."),
            *FPackageName::GetLongPackageAssetName(Package->GetName())));
    }
    RemoveSnapshots([](const FSnapshot& One) { return !One.Object.IsValid() || One.Frame != GFrameCounter; });
}

bool Tick(float)
{
    P4InitializeCreatorSource();
    P4InitializeCreatorMaterialFactories();
    FlushReadOnlyGuards();
    return true;
}
} //namespace

bool IsLockedPackage(const UPackage* Package)
{
    return Package && Owners.Contains(Package->GetFName());
}

bool IsLockedPackageName(FName Package)
{
    return Owners.Contains(Package);
}

bool IsLockedObject(const UObject* Object)
{
    if (!Object)
        return false;
    const UPackage* Package = Object->GetOutermost();
    if (Owners.Contains(Package->GetFName()))
        return true;
    // Defaults of the game's own classes are not saved anywhere a mod could carry.
    return Package->GetFName() == GameScript && Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject);
}

void Tell(const FString& Message)
{
    Messages.Add(Message);
    UE_LOG(LogTemp, Warning, TEXT("Kadan: %s"), *Message);
    if (FSlateApplication::IsInitialized() && FApp::CanEverRender())
    {
        FNotificationInfo Info(FText::FromString(Message));
        Info.ExpireDuration = 8.f;
        Info.bUseLargeFont = false;
        Info.WidthOverride = 480.f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
}

TArray<FString> TakeReadOnlyMessages()
{
    TArray<FString> Out = MoveTemp(Messages);
    Messages.Reset();
    return Out;
}

void FlushReadOnlyGuards()
{
    if (!Installed || !IsInGameThread())
        return;
    PutBack();
    for (const auto& Pair : MeshNotifications)
        if (UPackage* Package = Pair.Key.Get())
            Package->bIsCookedForEditor = Pair.Value;
    MeshNotifications.Reset();
    FlushEditGuards();
    // A file the importer made writable to write it again is read-only again.
    for (const FString& File : Unprotected)
        SetFileReadOnly(File, true);
    Unprotected.Reset();
}

void UpdateReadOnlyAsset(UObject* Asset)
{
    if (!Installed || !IsInGameThread() || !IsValid(Asset) || !Asset->IsAsset())
        return;
    // A copy is project content. A package read into a transient path (a saved file read back
    // under another name) is not a copy and keeps the record it was saved with.
    if (!P4ImportPublication::IsImporting() && P4IsContentPackageIgnoringCase(Asset->GetOutermost()->GetName()))
    {
        ForgetCopiedSource(Asset);
        CarryCopiedGameData(Asset);
    }
    // Upgrade only a loaded asset with explicit original identity or a known proxy role.
    // Intrinsic editor data then travels with native duplicates, independently of metadata.
    if (!P4HasCreatorSource(Asset))
    {
        FP4OriginalRecord Record;
        FString Error;
        if (P4ReadOriginalRecord(Asset, Record, Error))
        {
            UMetaData* Meta = Asset->GetOutermost()->MetaData;
            const bool Proxy = Meta && !Meta->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad) &&
                FString(Meta->GetValue(Asset, TEXT("P4BlueprintStandIn"))) == TEXT("1");
            P4RecordKnownGameSource(Asset, Record.Package, Proxy);
        }
        else if (UMetaData* Meta = Asset->GetOutermost()->MetaData)
            if (!Meta->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad) &&
                FString(Meta->GetValue(Asset, TEXT("P4BlueprintStandIn"))) == TEXT("1"))
                P4RecordKnownGameSource(Asset, Asset->GetOutermost()->GetName(), true);
    }
    UPackage* Package = Asset->GetOutermost();
    const FName Key = Package->GetFName();
    const bool Locked = !GetAssetPolicy(Asset).AllowEdit;
    // Lock this package, not the containing folder. Keep the lock across GC.
    if (Locked || !HoldsLockedAsset(Package))
        SetPackageLock(Key, Locked);
}

// Only a completed supported source operation calls this internal boundary. Its new
// data is the current reference representation, even when that target cannot export.
// Keep the target's identity/file locks and take a new baseline on its next edit.
void AcceptCompletedCreatorSource(UObject* Asset)
{
    if (!Installed || !IsInGameThread() || !IsValid(Asset) || !P4HasCreatorSource(Asset))
        return;
    UPackage* Package = Asset->GetOutermost();
    RemoveSnapshots(
        [Package](const FSnapshot& One) { return !One.Object.IsValid() || One.Object->GetOutermost() == Package; });
}

void RefreshReadOnlyAssets()
{
    if (!Installed || !IsInGameThread())
        return;
    for (TObjectIterator<UObject> It; It; ++It)
        if (It->IsAsset())
            UpdateReadOnlyAsset(*It);
}

void InitializeMutationGuards()
{
    if (Installed || !IsInGameThread())
        return;
    Installed = true;
#if P4_KADAN_TEST_SEAMS
    CopyTestSeams.SetCompletionFailurePackage = [](const FString& Package)
    {
        if (IsInGameThread())
            CopyCompletionFailurePackage = Package;
    };
    IModularFeatures::Get().RegisterModularFeature(FP4KadanCopyTestSeams::FeatureName(), &CopyTestSeams);
#endif
    P4InitializeCreatorSource();
    P4InitializeCreatorMaterialFactories();
    PreviousSave = FCoreUObjectDelegates::IsPackageOKToSaveDelegate;
    OurSave = FCoreUObjectDelegates::FIsPackageOKToSaveDelegate::CreateLambda(
        [](UPackage* Package, const FString& File, FOutputDevice* Output)
        {
            if (Package && P4ImportPublication::IsSaving(Package))
            {
                // The editor's previous veto also checks this same writable-path
                // filter. Suspend only our entry for this native publication call;
                // other plugins' restrictions and the previous delegate still apply.
                const FName* Found = Owners.Find(Package->GetFName());
                const FName Owner = Found ? *Found : NAME_None;
                const auto Filter = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools")
                                        .Get()
                                        .GetWritableFolderBlacklist();
                if (!Owner.IsNone())
                    Filter->UnregisterOwner(Owner);
                ON_SCOPE_EXIT
                {
                    if (!Owner.IsNone())
                        Filter->AddBlacklistItem(Owner, Package->GetName());
                };
                // Kadan's importer writes a game asset again (a stand-in it fills). The file is made writable for
                // this save, and read-only again once it is written.
                if (!Owner.IsNone())
                {
                    const FString Full = FPaths::ConvertRelativePathToFull(File);
                    if (FPlatformFileManager::Get().GetPlatformFile().IsReadOnly(*Full))
                    {
                        SetFileReadOnly(Full, false);
                        Unprotected.Add(Full);
                    }
                }
                return !PreviousSave.IsBound() || PreviousSave.Execute(Package, File, Output);
            }
            if (PreviousSave.IsBound() && !PreviousSave.Execute(Package, File, Output))
                return false;
            if (!Package)
                return true;
            if (!PackageIsLocked(Package))
                return true;
            UE_LOG(LogTemp, Warning,
                TEXT(
                    "Kadan: %s is a read-only game reference. Import your own source file to replace supported asset data."),
                *Package->GetName());
            return false;
        });
    FCoreUObjectDelegates::IsPackageOKToSaveDelegate = OurSave;
    PackageSaved = UPackage::PackageSavedEvent.AddLambda(
        [](const FString& File, UObject* Outer)
        {
            UPackage* Package = Cast<UPackage>(Outer);
            if (!Package || !Installed)
                return;
            // What the importer saves of a locked asset is the game's asset as imported.
            RemoveSnapshots([Package](const FSnapshot& One)
                { return !One.Object.IsValid() || One.Object->GetOutermost() == Package; });
            if (IsLockedPackage(Package))
            {
                const FString Full = FPaths::ConvertRelativePathToFull(File);
                SetFileReadOnly(Full, true);
                Unprotected.Remove(Full);
            }
        });
    Delete = FEditorDelegates::OnAssetsCanDelete.AddLambda(
        [](const TArray<UObject*>& Objects, FCanDeleteAssetResult& Result)
        {
            for (UObject* Object : Objects)
                if (IsValid(Object) && (IsLockedObject(Object) || !GetAssetPolicy(Object).AllowEdit))
                {
                    Result.Set(false);
                    Tell(FString::Printf(
                        TEXT("Kadan kept %s because it is a game asset, and game assets cannot be deleted."),
                        *AssetName(Object)));
                    return;
                }
        });
    Modified = FCoreUObjectDelegates::OnObjectModified.AddStatic(&Snapshot);
    PreModified = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [](UObject* Object, const FEditPropertyChain&) { Snapshot(Object); });
    Loaded = FCoreUObjectDelegates::OnAssetLoaded.AddStatic(&UpdateReadOnlyAsset);
    Saved = FCoreUObjectDelegates::OnObjectSaved.AddStatic(&UpdateReadOnlyAsset);
    Tags = UObject::FAssetRegistryTag::OnGetExtraObjectTags.AddLambda(
        [](const UObject* Asset, TArray<UObject::FAssetRegistryTag>& Out)
        {
            if (IsInGameThread() && Asset && Asset->IsAsset())
                Out.Add(UObject::FAssetRegistryTag(LockTag,
                    GetAssetPolicy(const_cast<UObject*>(Asset)).AllowEdit ? TEXT("0") : TEXT("1"),
                    UObject::FAssetRegistryTag::TT_Hidden));
        });
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    Registry.SearchAllAssets(true);
    TArray<FAssetData> Assets;
    Registry.GetAllAssets(Assets, true);
    for (const auto& Asset : Assets)
        RegistryAsset(Asset);
    Added = Registry.OnAssetAdded().AddStatic(&RegistryAsset);
    // A deleted asset (only a creator's can be) leaves nothing to lock.
    Removed = Registry.OnAssetRemoved().AddLambda(
        [](const FAssetData& Asset) { SetPackageLock(Asset.PackageName, false); });
    RefreshReadOnlyAssets();
    InitializeEditGuards();
    Ticking = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.f);
}

void ShutdownMutationGuards()
{
    if (!Installed)
        return;
#if P4_KADAN_TEST_SEAMS
    IModularFeatures::Get().UnregisterModularFeature(FP4KadanCopyTestSeams::FeatureName(), &CopyTestSeams);
    CopyCompletionFailurePackage.Reset();
#endif
    ShutdownEditGuards();
    P4ShutdownCreatorSource();
    P4ShutdownCreatorMaterialFactories();
    FTicker::GetCoreTicker().RemoveTicker(Ticking);
    FCoreUObjectDelegates::OnObjectModified.Remove(Modified);
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(PreModified);
    FCoreUObjectDelegates::OnAssetLoaded.Remove(Loaded);
    FCoreUObjectDelegates::OnObjectSaved.Remove(Saved);
    UPackage::PackageSavedEvent.Remove(PackageSaved);
    FEditorDelegates::OnAssetsCanDelete.Remove(Delete);
    UObject::FAssetRegistryTag::OnGetExtraObjectTags.Remove(Tags);
    if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
        FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().OnAssetAdded().Remove(Added);
    if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
        FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().OnAssetRemoved().Remove(Removed);
    // Preserve a later plugin's delegate; its shutdown restores ours before this module unloads.
    if (FCoreUObjectDelegates::IsPackageOKToSaveDelegate.GetHandle() == OurSave.GetHandle())
        FCoreUObjectDelegates::IsPackageOKToSaveDelegate = PreviousSave;
    if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
    {
        auto& Filter =
            FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get().GetWritableFolderBlacklist();
        for (const auto& Entry : Owners)
            Filter->UnregisterOwner(Entry.Value);
    }
    // The files stay read-only: they are the game's assets whether Kadan is loaded or not.
    for (const FString& File : Unprotected)
        SetFileReadOnly(File, true);
    for (const auto& Pair : MeshNotifications)
        if (UPackage* Package = Pair.Key.Get())
            Package->bIsCookedForEditor = Pair.Value;
    MeshNotifications.Reset();
    Owners.Reset();
    Snapshots.Reset();
    SnapshotObjects.Reset();
    Unprotected.Reset();
    PreviousSave.Unbind();
    OurSave.Unbind();
    Installed = false;
}
} //namespace P4Kadan
