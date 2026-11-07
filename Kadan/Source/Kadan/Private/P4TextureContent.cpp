#include "P4KadanReimportAsset.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4TextureContent.h"
#include "P4OriginalTextureImport.h"
#include "P4OriginalRecord.h"
#include "P4CreatorSource.h"
#include "P4StandInFill.h"
#include "P4TextureBuild.h"
#include "P4NativeHash.h"
#include "P4PackageSave.h"
#include "Engine/Texture.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
const FName ImportDataName(TEXT("AssetImportData")), UserDataName(TEXT("AssetUserData")),
    ImportedSizeName(TEXT("ImportedSize"));
const FName SourceName(TEXT("Source")), StoredAsPngName(TEXT("bPNGCompressed"));
const TCHAR* const BaselineKey = TEXT("P4NativeTextureBaseline");
const TCHAR* const BaselineOwner = TEXT("p4mapkit-original-texture-v1");

bool Unsaved(const FProperty* Property)
{
    return Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated);
}
bool WriteAll(const TFunction<bool(const uint8*, int32)>& Write, const uint8* Bytes, int64 Count)
{
    const int64 Step = 64 * 1024; // the largest piece the hash takes at once
    for (int64 At = 0; At < Count; At += Step)
        if (!Write(Bytes + At, int32(FMath::Min(Step, Count - At))))
            return false;
    return true;
}
bool WriteCount(const TFunction<bool(const uint8*, int32)>& Write, int64 Count)
{
    return Write(reinterpret_cast<const uint8*>(&Count), int32(sizeof Count));
}

// Every setting the texture saves, by name and exact value, with names and object references as text.
bool Settings(UTexture* Texture, TArray<uint8>& Out)
{
    FMemoryWriter Memory(Out, true);
    FObjectAndNameAsStringProxyArchive Writer(Memory, false);
    FString Class = Texture->GetClass()->GetPathName();
    Writer << Class;
    for (TFieldIterator<FProperty> Field(Texture->GetClass()); Field; ++Field)
    {
        if (Unsaved(*Field) || Field->GetFName() == ImportDataName || Field->GetFName() == ImportedSizeName)
            continue;
        FString Name = Field->GetName();
        Writer << Name;
        const FStructProperty* Struct = CastField<FStructProperty>(*Field);
        for (int32 Index = 0; Index < Field->ArrayDim; ++Index)
        {
            void* Value = Field->ContainerPtrToValuePtr<void>(Texture, Index);
            if (Field->GetFName() == UserDataName)
            {
                const FArrayProperty* Array = CastField<FArrayProperty>(*Field);
                const FObjectPropertyBase* Object = Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
                if (Array && Object)
                {
                    // The intrinsic origin is editor policy, not a texture setting read by
                    // the game's cook. Keep every other entry, in its original order, and
                    // use the same native property serializer on a private scratch array.
                    // An empty source array therefore retains its previous exact bytes.
                    FScriptArray Scratch;
                    FScriptArrayHelper Filtered(Array, &Scratch), Existing(Array, Value);
                    ON_SCOPE_EXIT
                    {
                        Filtered.EmptyValues();
                    };
                    for (int32 At = 0; At < Existing.Num(); ++At)
                    {
                        const UObject* Data = Object->GetObjectPropertyValue(Existing.GetRawPtr(At));
                        if (IsValid(Data) && Data->GetClass() == UP4CreatorSourceData::StaticClass() &&
                            Data->GetOuter() == Texture && Data->IsEditorOnly())
                            continue;
                        Array->Inner->CopyCompleteValue(
                            Filtered.GetRawPtr(Filtered.AddValue()), Existing.GetRawPtr(At));
                    }
                    Field->SerializeItem(FStructuredArchiveFromArchive(Writer).GetSlot(), &Scratch, nullptr);
                    continue;
                }
            }
            if (Field->GetFName() != SourceName || !Struct)
            {
                Field->SerializeItem(FStructuredArchiveFromArchive(Writer).GetSlot(), Value, nullptr);
                continue;
            }
            // The source image's own record: its size, format and id. Saving may store the pixels as
            // a PNG, which changes nothing a cook reads.
            for (TFieldIterator<FProperty> Member(Struct->Struct); Member; ++Member)
            {
                if (Unsaved(*Member) || Member->GetFName() == StoredAsPngName)
                    continue;
                FString MemberName = Member->GetName();
                Writer << MemberName;
                for (int32 At = 0; At < Member->ArrayDim; ++At)
                    Member->SerializeItem(FStructuredArchiveFromArchive(Writer).GetSlot(),
                        Member->ContainerPtrToValuePtr<void>(Value, At), nullptr);
            }
        }
    }
    return !Writer.IsError() && !Memory.IsError();
}

// The game's files a texture is read from, in one digest. A file that is not there counts as absent.
bool GameFiles(const FString& HeaderFile, const FString& BodyFile, FString& Digest)
{
    const FString Files[] = {HeaderFile, BodyFile, FPaths::ChangeExtension(HeaderFile, TEXT("ubulk")),
        FPaths::ChangeExtension(HeaderFile, TEXT("uptnl")), FPaths::ChangeExtension(HeaderFile, TEXT("m.ubulk"))};
    return P4HashByteStream(
        [&](const TFunction<bool(const uint8*, int32)>& Write)
        {
            for (int32 Index = 0; Index < 5; ++Index)
            {
                TArray<uint8> Bytes;
                const bool There = IFileManager::Get().FileExists(*Files[Index]);
                if (!There && Index < 2)
                    return false;
                if (There && !FFileHelper::LoadFileToArray(Bytes, *Files[Index], FILEREAD_Silent))
                    return false;
                if (!WriteCount(Write, There ? int64(Bytes.Num()) : int64(-1)) ||
                    !WriteAll(Write, Bytes.GetData(), Bytes.Num()))
                    return false;
            }
            return true;
        },
        Digest);
}

struct FBaseline
{
    FString Package, Class, Source, State, GameFormat, GameGroup;
    int32 Width = 0, Height = 0, Slices = 0, Mips = 0, FirstMip = 0;
};
FString BaselineText(const FBaseline& Value)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("format"), 1);
    Json->SetStringField(TEXT("owner"), BaselineOwner);
    Json->SetStringField(TEXT("package"), Value.Package);
    Json->SetStringField(TEXT("cls"), Value.Class);
    Json->SetStringField(TEXT("source_sha256"), Value.Source);
    Json->SetStringField(TEXT("state"), Value.State);
    Json->SetStringField(TEXT("game_format"), Value.GameFormat);
    Json->SetStringField(TEXT("game_texture_group"), Value.GameGroup);
    Json->SetNumberField(TEXT("width"), Value.Width);
    Json->SetNumberField(TEXT("height"), Value.Height);
    Json->SetNumberField(TEXT("slices"), Value.Slices);
    Json->SetNumberField(TEXT("mips"), Value.Mips);
    Json->SetNumberField(TEXT("first_mip"), Value.FirstMip);
    FString Out = P4CondensedJson(Json);
    return Out;
}
// The record an import left on the texture. False when there is none or it is not this texture's.
bool ReadBaseline(const UTexture* Texture, FBaseline& Out)
{
    const FString Record = Texture->GetOutermost()->GetMetaData()->GetValue(Texture, BaselineKey);
    TSharedPtr<FJsonObject> Json;
    FString Owner;
    double Format = 0;
    if (Record.IsEmpty() || !P4ParseJson(Record, Json))
        return false;
    if (!Json->TryGetNumberField(TEXT("format"), Format) || Format != 1 ||
        !Json->TryGetStringField(TEXT("owner"), Owner) || !P4SameText(Owner, BaselineOwner) ||
        !Json->TryGetStringField(TEXT("package"), Out.Package) || !Json->TryGetStringField(TEXT("cls"), Out.Class) ||
        !Json->TryGetStringField(TEXT("source_sha256"), Out.Source) ||
        !Json->TryGetStringField(TEXT("state"), Out.State) ||
        !Json->TryGetStringField(TEXT("game_format"), Out.GameFormat) ||
        !Json->TryGetStringField(TEXT("game_texture_group"), Out.GameGroup) ||
        !Json->TryGetNumberField(TEXT("width"), Out.Width) || !Json->TryGetNumberField(TEXT("height"), Out.Height) ||
        !Json->TryGetNumberField(TEXT("slices"), Out.Slices) || !Json->TryGetNumberField(TEXT("mips"), Out.Mips) ||
        !Json->TryGetNumberField(TEXT("first_mip"), Out.FirstMip))
        return false;
    return P4IsSha256Hex(Out.Source) && P4IsSha256Hex(Out.State) &&
        P4SameText(Out.Package, Texture->GetOutermost()->GetName()) &&
        P4SameText(Out.Class, Texture->GetClass()->GetPathName());
}
void BaselineFields(const FBaseline& Value, FJsonObject& Json)
{
    Json.SetStringField(TEXT("source_sha256"), Value.Source);
    Json.SetStringField(TEXT("imported_state"), Value.State);
    Json.SetStringField(TEXT("game_format"), Value.GameFormat);
    Json.SetStringField(TEXT("game_texture_group"), Value.GameGroup);
    Json.SetNumberField(TEXT("width"), Value.Width);
    Json.SetNumberField(TEXT("height"), Value.Height);
    Json.SetNumberField(TEXT("slices"), Value.Slices);
    Json.SetNumberField(TEXT("mips"), Value.Mips);
    Json.SetNumberField(TEXT("first_mip"), Value.FirstMip);
}

// Gives To every setting and every source pixel of From. From is a texture the import just built, with
// one block and one layer.
bool Copy(UTexture* To, UTexture* From, FString& Error)
{
    FTextureSource& In = From->Source;
    if (In.GetNumBlocks() != 1 || In.GetNumLayers() != 1)
    {
        Error = TEXT("texture-import-source-has-blocks-or-layers");
        return false;
    }
    TArray64<uint8> Pixels;
    for (int32 Mip = 0; Mip < In.GetNumMips(); ++Mip)
    {
        TArray64<uint8> One;
        if (!In.GetMipData(One, Mip) || One.Num() != In.CalcMipSize(Mip))
        {
            Error = TEXT("texture-import-built-source-unreadable");
            return false;
        }
        Pixels.Append(One);
    }
    // A texture that is being replaced may still be compressing its old pixels for display.
    To->FinishCachePlatformData();
    for (TFieldIterator<FProperty> Field(From->GetClass()); Field; ++Field)
    {
        if (Unsaved(*Field) || Field->GetFName() == ImportDataName || Field->GetFName() == SourceName)
            continue;
        Field->CopyCompleteValue(Field->ContainerPtrToValuePtr<void>(To), Field->ContainerPtrToValuePtr<void>(From));
    }
    To->Source.Init(In.GetSizeX(), In.GetSizeY(), In.GetNumSlices(), In.GetNumMips(), In.GetFormat(), Pixels.GetData());
    To->Source.SetId(In.GetId(), true);
    // Rekey running platform data immediately. Any later refusal/save can
    // destroy the fill guard, whose cache finish must see the current source.
    To->UpdateResource();
    return true;
}
FString Finish(const TSharedRef<FJsonObject>& Json, const FString& Error)
{
    Json->SetBoolField(TEXT("valid"), Error.IsEmpty());
    Json->SetStringField(TEXT("error"), Error);
    FString Out = P4CondensedJson(Json);
    return Out;
}
} //namespace

FString P4TextureState(UTexture* Texture)
{
    if (!IsInGameThread() || !IsValid(Texture) || !Texture->Source.IsValid())
        return FString();
    TArray<uint8> Saved;
    if (!Settings(Texture, Saved))
        return FString();
    FTextureSource& Source = Texture->Source;
    FString Digest;
    const bool Good = P4HashByteStream(
        [&](const TFunction<bool(const uint8*, int32)>& Write)
        {
            if (!WriteCount(Write, Saved.Num()) || !WriteAll(Write, Saved.GetData(), Saved.Num()))
                return false;
            for (int32 Block = 0; Block < Source.GetNumBlocks(); ++Block)
            {
                FTextureSourceBlock Info;
                Source.GetBlock(Block, Info);
                for (int32 Layer = 0; Layer < Source.GetNumLayers(); ++Layer)
                    for (int32 Mip = 0; Mip < Info.NumMips; ++Mip)
                    {
                        TArray64<uint8> Pixels;
                        if (!Source.GetMipData(Pixels, Block, Layer, Mip) || !WriteCount(Write, Pixels.Num()) ||
                            !WriteAll(Write, Pixels.GetData(), Pixels.Num()))
                            return false;
                    }
            }
            return true;
        },
        Digest);
    return Good ? Digest : FString();
}

bool P4TextureSaveProperty(const UTexture* Texture, const FProperty* Property, bool& Skip, FString& Error)
{
    Skip = false;
    // The record of the imported file is the texture's own subobject and is saved with it.
    if (Property->GetFName() == ImportDataName)
    {
        Skip = true;
        return true;
    }
    if (Property->GetFName() != UserDataName)
        return true;
    const FArrayProperty* Array = CastField<FArrayProperty>(Property);
    if (!Array || FScriptArrayHelper(Array, Property->ContainerPtrToValuePtr<void>(Texture)).Num() != 0)
    {
        Error = TEXT("A texture with asset user data cannot be saved by the native save yet");
        return false;
    }
    Skip = true;
    return true;
}

FString P4OriginalTextureStatus(UTexture* Texture)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("original"), false);
    Json->SetBoolField(TEXT("unchanged"), false);
    if (!IsInGameThread())
        return Finish(Json, TEXT("texture-status-requires-editor-thread"));
    if (!IsValid(Texture))
        return Finish(Json, TEXT("texture-status-needs-a-texture"));
    Json->SetStringField(TEXT("asset"), Texture->GetPathName());
    FBaseline Baseline;
    if (ReadBaseline(Texture, Baseline))
        BaselineFields(Baseline, *Json);
    const FP4OriginalStatus Status = P4QueryOriginalAsset(Texture);
    P4OriginalStatusFields(Status, *Json);
    return Finish(Json, Status.Error);
}

bool P4OriginalTextureMatchesSource(
    UTexture* Texture, const FString& HeaderFile, const FString& BodyFile, FString& Error)
{
    Error.Reset();
    FBaseline Baseline;
    FP4OriginalRecord Record;
    FString Source;
    if (!IsInGameThread() || !IsValid(Texture) || !ReadBaseline(Texture, Baseline) ||
        !P4ReadOriginalRecord(Texture, Record, Error) || Record.Kind != TEXT("texture") || Record.ReferenceOnly ||
        Record.SourceDigestKind != TEXT("texture-source-v1") || Record.ComparisonSchema != TEXT("texture-state-v1") ||
        !P4SameText(Record.SourceDigest, Baseline.Source) || !P4SameText(Record.ComparisonDigest, Baseline.State) ||
        !GameFiles(HeaderFile, BodyFile, Source) || !P4SameText(Source, Baseline.Source) ||
        !P4SameText(P4TextureState(Texture), Baseline.State))
    {
        if (Error.IsEmpty())
            Error = TEXT("shader-used-texture-is-not-unchanged-from-this-native-source");
        return false;
    }
    return true;
}

FP4OriginalTextureImportResult P4ImportOriginalTextureNative(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    FP4OriginalTextureImportResult Result;
    auto Fail = [&](EP4OriginalTextureImportError Code, const FString& Message)
    {
        Result.Error = Code;
        Result.ErrorDetail = Message;
        return Result;
    };
    if (!IsInGameThread())
        return Fail(EP4OriginalTextureImportError::RequiresEditorThread, TEXT("texture-import-requires-editor-thread"));
    const FString Body = BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile;
    FString Digest, Again, Error;
    if (!GameFiles(HeaderFile, Body, Digest))
        return Fail(EP4OriginalTextureImportError::SourceUnreadable, TEXT("texture-import-source-unreadable"));

    FP4TextureBuildReport Report;
    TStrongObjectPtr<UTexture> Built(P4BuildOriginalTexture(HeaderFile, Body, Report, Error));
    // The built texture is only what the one in Content is made from.
    struct FDiscard
    {
        UTexture* Texture;
        ~FDiscard()
        {
            if (Texture)
                Texture->MarkPendingKill();
        }
    } Discard{Built.Get()};
    Result.HasBuildReport = true;
    Result.Class = Report.Class;
    Result.Package = Report.Package;
    if (!Built.IsValid())
        return Fail(EP4OriginalTextureImportError::BuildFailed, Error);
    if (!GameFiles(HeaderFile, Body, Again) || !P4SameText(Again, Digest))
        return Fail(EP4OriginalTextureImportError::SourceChanged, TEXT("texture-import-source-changed-while-reading"));
    Result.LargestBuiltDifference = Report.LargestBuiltDifference;
    // A texture the engine would cook to another format, size or mip count than the game's would go
    // into a mod wrong once it is edited, so it is not brought in at all.
    if (!Report.Equal)
        return Fail(EP4OriginalTextureImportError::BuildDiffers, TEXT("texture-import-engine-build-differs-from-game"));
    if (!P4IsValidContentPackage(Report.Package) ||
        !FPackageName::GetLongPackageAssetName(Report.Package)
            .Equals(Report.Object, ESearchCase::IgnoreCase)) // names ignore case; the texture keeps the game's spelling
        return Fail(EP4OriginalTextureImportError::UnusableGamePath, TEXT("texture-import-game-path-unusable"));

    // The id the engine keys its compressed results by is made from the pixels, so the same game
    // texture imported twice is the same texture.
    Built->Source.UseHashAsGuid();
    FBaseline Baseline;
    Baseline.Package = Report.Package;
    Baseline.Class = Report.Class;
    Baseline.Source = Digest;
    Baseline.State = P4TextureState(Built.Get());
    Baseline.GameFormat = Report.GameFormat;
    Baseline.GameGroup = Report.GameTextureGroup;
    Baseline.Width = Report.Width;
    Baseline.Height = Report.Height;
    Baseline.Slices = Report.Slices;
    Baseline.Mips = Report.Mips;
    Baseline.FirstMip = Report.FirstMip;
    if (Baseline.State.IsEmpty())
        return Fail(
            EP4OriginalTextureImportError::BuiltSourceUnreadable, TEXT("texture-import-built-source-unreadable"));
    FP4OriginalTextureImportBaseline PublicBaseline;
    PublicBaseline.SourceDigest = Baseline.Source;
    PublicBaseline.State = Baseline.State;
    PublicBaseline.GameFormat = Baseline.GameFormat;
    PublicBaseline.GameGroup = Baseline.GameGroup;
    PublicBaseline.Width = Baseline.Width;
    PublicBaseline.Height = Baseline.Height;
    PublicBaseline.Slices = Baseline.Slices;
    PublicBaseline.Mips = Baseline.Mips;
    PublicBaseline.FirstMip = Baseline.FirstMip;
    Result.Baseline = MoveTemp(PublicBaseline);

    const FString Path = Report.Package + TEXT(".") + Report.Object;
    Result.HasAsset = true;
    Result.AssetPath = Path;
    const bool OnDisk = !P4KadanOwnsReimportSlot(Report.Package) && FPackageName::DoesPackageExist(Report.Package);
    UObject* Found = FindObject<UObject>(nullptr, *Path);
    if (Found && !IsValid(Found))
        Found = nullptr;
    if (!Found && OnDisk)
        Found = StaticLoadObject(UObject::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    UTexture* Target = Cast<UTexture>(Found);
    bool Created = false, Replaced = false, StandIn = false;
    TUniquePtr<FP4StandInFill> FillGuard;
    if (Found)
    {
        StandIn = FString(Found->GetOutermost()->GetMetaData()->GetValue(Found, TEXT("P4BlueprintStandIn"))) ==
            TEXT("1");
        if (StandIn)
        {
            if (!Target || Target->GetClass() != Built->GetClass())
                return Fail(EP4OriginalTextureImportError::PlaceholderClassDiffers,
                    TEXT("texture-import-placeholder-class-differs"));
            if (!P4IsVerifiedStandIn(Target))
                return Fail(EP4OriginalTextureImportError::EditedPlaceholder,
                    TEXT("texture-import-would-replace-an-edited-placeholder"));
            if (!SaveToDisk)
                return Fail(EP4OriginalTextureImportError::PlaceholderFillNeedsSave,
                    TEXT("texture-import-placeholder-fill-requires-save"));
            FillGuard = MakeUnique<FP4StandInFill>(Target);
            if (!FillGuard->IsReady())
                return Fail(EP4OriginalTextureImportError::PlaceholderBackupUnreadable,
                    TEXT("texture-import-placeholder-backup-unreadable"));
            Replaced = true;
        }
        else
        {
            FBaseline Old;
            if (!Target || Target->GetClass() != Built->GetClass() || !ReadBaseline(Target, Old))
                return Fail(EP4OriginalTextureImportError::DestinationOccupied,
                    TEXT("texture-import-destination-holds-another-asset"));
            const FP4OriginalStatus Status = P4QueryOriginalAsset(Target);
            FString MigrationError;
            const bool Untouched = Status.IsUnchanged() ||
                ((Status.State == EP4OriginalState::NeedsMigration || Status.State == EP4OriginalState::Recorded) &&
                    P4MigrateOriginalRecord(
                        Target, [&]() { return P4SameText(P4TextureState(Target), Old.State); }, MigrationError));
            if (!Untouched)
                return Fail(Status.State == EP4OriginalState::NeedsMigration
                        ? EP4OriginalTextureImportError::RecordNeedsValidation
                        : EP4OriginalTextureImportError::EditedTexture,
                    Status.State == EP4OriginalState::NeedsMigration
                        ? TEXT("texture-import-record-needs-validation: ") + MigrationError
                        : FString(TEXT("texture-import-would-replace-an-edited-texture")));
            Replaced = !P4SameText(Old.Source, Baseline.Source) || !P4SameText(Old.State, Baseline.State);
        }
    }
    else
    {
        // A package of that name with something else in it, saved or not, is not this texture's place.
        UPackage* Loaded = FindPackage(nullptr, *Report.Package);
        TArray<UObject*> Inside;
        if (Loaded)
            GetObjectsWithOuter(Loaded, Inside, false);
        Inside.RemoveAll([](const UObject* Object) { return !IsValid(Object) || Object->IsA<UMetaData>(); });
        if (OnDisk || Inside.Num() != 0)
            return Fail(EP4OriginalTextureImportError::DestinationOccupied,
                TEXT("texture-import-destination-holds-another-asset"));
        Target = NewObject<UTexture>(CreatePackage(*Report.Package), Built->GetClass(), FName(*Report.Object),
            RF_Public | RF_Standalone | RF_Transactional);
        Created = Target != nullptr;
        if (!Created)
            return Fail(EP4OriginalTextureImportError::CreateFailed, TEXT("texture-import-could-not-make-the-texture"));
    }
    TStrongObjectPtr<UTexture> Guard(Target);
    if (Created || Replaced)
    {
        const bool Copied = Copy(Target, Built.Get(), Error) && P4SameText(P4TextureState(Target), Baseline.State);
        if (!Copied)
        {
            if (Created)
            {
                // Out of its place, so that a later attempt finds the path free.
                Target->ClearFlags(RF_Public | RF_Standalone);
                Target->Rename(nullptr, GetTransientPackage(),
                    REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
                Target->MarkPendingKill();
            }
            return Fail(EP4OriginalTextureImportError::CopyFailed,
                Error.IsEmpty() ? FString(TEXT("texture-import-copy-differs-from-built-texture")) : Error);
        }
        Target->GetOutermost()->GetMetaData()->SetValue(Target, BaselineKey, *BaselineText(Baseline));
        if (!P4MarkLegacyOriginalAsset(Target, TArray<FP4OriginalValueNote>(), Error))
            return Fail(EP4OriginalTextureImportError::RecordFailed, Error);
        if (StandIn)
            Target->GetOutermost()->GetMetaData()->RemoveValue(Target, FName(TEXT("P4BlueprintStandIn")));
        if (Created)
            FAssetRegistryModule::AssetCreated(Target);
        Target->MarkPackageDirty();
    }
    Result.HasMutationReport = true;
    Result.Created = Created;
    Result.Replaced = Replaced;
    Result.FilledStandIn = StandIn;
    Result.State = Baseline.State;
    FString FillSourceError;
    if (FillGuard && !P4PrepareVerifiedGameFillSource(Target, *FillGuard, FillSourceError))
        return Fail(EP4OriginalTextureImportError::RecordFailed, FillSourceError);
    if (SaveToDisk && (Created || Replaced || Target->GetOutermost()->IsDirty() || !OnDisk))
    {
        FString SaveError;
        if (!P4SaveAssetPackage(
                Target, [&]() { return P4SameText(P4TextureState(Target), Baseline.State); }, SaveError))
            return Fail(EP4OriginalTextureImportError::SaveRefused, TEXT("texture-import-save-refused: ") + SaveError);
        Result.Saved = true;
    }
    if (FillGuard)
        FillGuard->Commit();
    Result.Asset = Target;
    return Result;
}
