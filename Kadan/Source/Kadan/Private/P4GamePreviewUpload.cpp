#include "P4GamePreviewUpload.h"
#include "P4Common.h"
#include "P4GameLink.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Base64.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FString LuaString(const FString& Text)
{
    return TEXT("\"") +
        Text.Replace(TEXT("\\"), TEXT("\\\\"))
            .Replace(TEXT("\""), TEXT("\\\""))
            .Replace(TEXT("\r"), TEXT("\\r"))
            .Replace(TEXT("\n"), TEXT("\\n"))
            .Replace(TEXT("\t"), TEXT("\\t")) +
        TEXT("\"");
}

bool ReadBool(const FP4GameReply& Reply)
{
    bool Value = false;
    return Reply.bOk && Reply.Value && Reply.Value->TryGetBool(Value) && Value;
}
} //namespace

FP4GamePreviewUpload::FP4GamePreviewUpload(FP4GameLink& InLink, FDone InDone)
    : Link(&InLink), Done(MoveTemp(InDone)), UploadId(FGuid::NewGuid().ToString(EGuidFormats::Digits))
{}

FP4GamePreviewUpload::~FP4GamePreviewUpload() = default;

FString FP4GamePreviewUpload::CacheEntry() const
{
    return TEXT("kadan_preview_bundles[") + LuaString(Plan.Fingerprint) + TEXT("]");
}

FString FP4GamePreviewUpload::OwnEntry() const
{
    return TEXT("local c=kadan_preview_bundles and ") + CacheEntry() + TEXT("; if not c or c.upload~=") +
        LuaString(UploadId) + TEXT(" then error('The preview asset upload changed owners.') end ");
}

FString FP4GamePreviewUpload::Failure(const FP4GameReply& Reply) const
{
    FString Detail;
    if (Reply.Whole)
        Reply.Whole->TryGetStringField(TEXT("detail"), Detail);
    return Detail.IsEmpty() ? Reply.Error : Detail;
}

void FP4GamePreviewUpload::Start(const FP4GamePreviewStartResult* FrozenStart)
{
    FString Error;
    if (!P4GatherGamePreviewContent(Plan, Error, FrozenStart))
    {
        Finish(false, Error);
        return;
    }
    // Stock game maps still work with a framework predating live asset bundles.
    if (Plan.SourcePackages.Num() == 0 && Plan.WorldSourceObjectPath.IsEmpty())
    {
        Finish(true);
        return;
    }
    if (Plan.Fingerprint.IsEmpty())
    {
        Finish(false, TEXT("Imported assets did not produce a preview fingerprint."));
        return;
    }
    CheckCache();
}

void FP4GamePreviewUpload::CheckCache()
{
    const FString WorldCheck = Plan.WorldSourceObjectPath.IsEmpty()
        ? FString()
        : TEXT(
              "if type(game.reserve_preview)~='function' then error('Update Tsuchi to preview an authored editor world.') end ");
    const FString Source =
        TEXT(
            "if type(asset.begin_bundle)~='function' then error('Update Tsuchi to use imported assets in live preview.') end ") +
        WorldCheck + TEXT("kadan_preview_bundles=kadan_preview_bundles or {}; local c=") + CacheEntry() +
        TEXT("; if c then local ok,s=pcall(asset.bundle_status,c.bundle); if ok and s and s.state=='mounted' then ") TEXT(
            "return {mounted=true,bundle=c.bundle,world_name=c.world_name or '',world_package=c.world_package or '',scene_object=c.scene_object or ''} end ")
            TEXT("pcall(asset.release_bundle,c.bundle); ") +
        CacheEntry() + TEXT("=nil end return false");
    const TWeakPtr<FP4GamePreviewUpload> Weak = AsShared();
    Link->Eval(Source,
        [Weak](const FP4GameReply& Reply)
        {
            const TSharedPtr<FP4GamePreviewUpload> Self = Weak.Pin();
            if (!Self || Self->bFinished)
                return;
            if (!Reply.bOk)
            {
                Self->Finish(false, Self->Failure(Reply));
                return;
            }
            const TSharedPtr<FJsonObject>* Cached = nullptr;
            if (Reply.Value && Reply.Value->TryGetObject(Cached))
            {
                if (!(*Cached)->TryGetStringField(TEXT("bundle"), Self->Bundle) ||
                    !Self->Bundle.StartsWith(TEXT("@asset_bundle:")))
                {
                    Self->Finish(false, TEXT("The mounted preview bundle cache is incomplete."));
                    return;
                }
                (*Cached)->TryGetStringField(TEXT("world_name"), Self->ReservedWorld);
                (*Cached)->TryGetStringField(TEXT("world_package"), Self->Content.WorldObjectPath);
                (*Cached)->TryGetStringField(TEXT("scene_object"), Self->Content.SceneObjectPath);
                if (!Self->Plan.WorldSourceObjectPath.IsEmpty() &&
                    (Self->ReservedWorld.IsEmpty() || Self->Content.WorldObjectPath.IsEmpty() ||
                        Self->Content.SceneObjectPath.IsEmpty()))
                {
                    Self->Finish(false, TEXT("The cached editor world has no native preview scene."));
                    return;
                }
                Self->bMounted = Self->bCached = true;
                Self->Activate();
                return;
            }
            if (Self->Plan.WorldSourceObjectPath.IsEmpty())
                Self->Cook();
            else
                Self->ReserveWorld();
        });
}

void FP4GamePreviewUpload::ReserveWorld(int32 Attempt)
{
    Progress = TEXT("Reserving an area for the editor world");
    // Keep at most two native handles. Once playing in one, a changed world is
    // cooked for the other area; the game never replaces its currently loaded scene.
    const FString Source = TEXT(
        "kadan_preview_scenes=kadan_preview_scenes or {}; local pool=kadan_preview_scenes; local current=game.world(); local chosen,info; local current_slot=false ")
        TEXT("for i=1,2 do local p=pool[i]; if p then local ok,s=pcall(game.preview_scene,p); if ok and s then ") TEXT(
            "if s.world_name~=current and not chosen then chosen,info=i,s elseif s.world_name==current then current_slot=true end ")
            TEXT("else pool[i]=nil end end end ") TEXT(
                "if not chosen then for i=1,2 do if not pool[i] then local p,e=game.reserve_preview(); if p then ")
                TEXT(
                    "pool[i]=p; local s,why=game.preview_scene(p); if not s then error(tostring(why)) end chosen,info=i,s; break ")
                    TEXT(
                        "elseif not current_slot then error('Could not reserve an editor preview area: '..tostring(e)) end end end end ")
                        TEXT(
                            "if not chosen then return {needs_neutral=true} end return {slot=chosen,world_name=info.world_name,scene_name=info.scene_name}");
    const TWeakPtr<FP4GamePreviewUpload> Weak = AsShared();
    Link->Eval(Source,
        [Weak, Attempt](const FP4GameReply& Reply)
        {
            const TSharedPtr<FP4GamePreviewUpload> Self = Weak.Pin();
            if (!Self || Self->bFinished)
                return;
            if (Reply.bTimedOut && Attempt < 2)
            {
                Self->ReserveWorld(Attempt + 1);
                return;
            }
            const TSharedPtr<FJsonObject>* Result = nullptr;
            if (!Reply.bOk || !Reply.Value || !Reply.Value->TryGetObject(Result))
            {
                Self->Finish(
                    false, Reply.bOk ? TEXT("The game returned no preview area reservation.") : Self->Failure(Reply));
                return;
            }
            bool bNeutral = false;
            (*Result)->TryGetBoolField(TEXT("needs_neutral"), bNeutral);
            if (bNeutral)
            {
                Self->bNeedsNeutralTravel = true;
                Self->Finish(false,
                    TEXT(
                        "The preview area is currently loaded. Travel to a neutral game map before preparing this editor world."));
                return;
            }
            double Slot = 0;
            (*Result)->TryGetNumberField(TEXT("slot"), Slot);
            (*Result)->TryGetStringField(TEXT("world_name"), Self->ReservedWorld);
            (*Result)->TryGetStringField(TEXT("scene_name"), Self->Plan.SceneRuntimeName);
            if (Slot < 1 || Slot > 2 || Self->ReservedWorld.IsEmpty() || Self->Plan.SceneRuntimeName.IsEmpty())
            {
                Self->Finish(false, TEXT("The game returned an incomplete preview area reservation."));
                return;
            }
            Self->PreviewSlot = int32(Slot);
            Self->Plan.WorldRuntimeName = Self->ReservedWorld;
            Self->Cook();
        });
}

void FP4GamePreviewUpload::Cook()
{
    Progress = TEXT("Cooking imported assets for preview");
    FString Error;
    if (!P4CookGamePreviewContent(Plan, Content, Error))
    {
        Finish(false, Error);
        return;
    }
    if (Content.Fingerprint != Plan.Fingerprint || Content.ContainerStem.IsEmpty() || Content.PackageRoot.IsEmpty())
    {
        Finish(false, TEXT("The preview cooker returned incomplete asset bundle details."));
        return;
    }
    if (!Plan.WorldSourceObjectPath.IsEmpty() &&
        (Content.WorldObjectPath.IsEmpty() || Content.SceneObjectPath.IsEmpty()))
    {
        Finish(false, TEXT("The preview cooker returned no world or native scene asset."));
        return;
    }
    if (!Plan.WorldSourceObjectPath.IsEmpty() &&
        FPackageName::GetShortName(FPackageName::ObjectPathToPackageName(Content.WorldObjectPath)) != ReservedWorld)
    {
        Finish(false, TEXT("The preview cooker changed the editor world's reserved area name."));
        return;
    }
    // Export names include project/fingerprint namespaces and can exceed the
    // runtime's 64-character spool-name limit. Each native upload has its own
    // directory, so the short stem need not reproduce the exported filename.
    SpoolStem = TEXT("kadan_") + FMD5::HashAnsiString(*Plan.Fingerprint);
    if (!Content.ShaderMetadataJson.IsEmpty())
    {
        TSharedPtr<FJsonObject> Shaders;
        if (!P4ParseJson(Content.ShaderMetadataJson, Shaders) || !Shaders)
        {
            Finish(false, TEXT("The preview cooker returned invalid shader metadata."));
            return;
        }
        Shaders->SetStringField(TEXT("archive"), SpoolStem + TEXT(".shaders"));
        Content.ShaderMetadataJson.Empty();
        FJsonSerializer::Serialize(Shaders.ToSharedRef(), TJsonWriterFactory<>::Create(&Content.ShaderMetadataJson));
    }
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    const FString Paths[] = {Content.PakFile, Content.UtocFile, Content.UcasFile, Content.ShaderFile};
    for (int32 Index = 0; Index != UE_ARRAY_COUNT(Paths); ++Index)
    {
        if (Index == 3 && Paths[Index].IsEmpty())
            continue;
        const int64 Size = Files.FileSize(*Paths[Index]);
        if (Size <= 0)
        {
            Finish(false, TEXT("A cooked preview bundle file is missing or empty: ") + Paths[Index]);
            return;
        }
        FPart Part;
        Part.Path = Paths[Index];
        Part.Kind = Index;
        Part.Size = Size;
        Parts.Add(MoveTemp(Part));
        Total += Size;
    }
    if (Total > (int64(1) << 30))
    {
        Finish(false, TEXT("The preview bundle exceeds the game's 1 GiB upload limit."));
        return;
    }
    for (const auto& Alias : Content.RuntimePaths)
        Aliases.Emplace(Alias.Key, Alias.Value);
    Aliases.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B) { return A.Key < B.Key; });
    BeginBundle();
}

void FP4GamePreviewUpload::BeginBundle(int32 Attempt)
{
    Progress = TEXT("Opening live asset upload");
    const FString WorldFields = Plan.WorldSourceObjectPath.IsEmpty()
        ? FString()
        : FString::Printf(TEXT(",preview=kadan_preview_scenes[%d],world_name=%s,world_package=%s,scene_object=%s"),
              PreviewSlot, *LuaString(ReservedWorld),
              *LuaString(FPackageName::ObjectPathToPackageName(Content.WorldObjectPath)),
              *LuaString(Content.SceneObjectPath));
    // Cache the native handle in the same request that creates it. Retrying a lost
    // reply returns that handle instead of allocating a second upload directory.
    const FString Source = TEXT("kadan_preview_bundles=kadan_preview_bundles or {}; local c=") + CacheEntry() +
        TEXT("; if c then if c.upload~=") + LuaString(UploadId) +
        TEXT(" then error('Another preview is preparing these assets.') end return c.bundle end ") +
        TEXT("local b,e=asset.begin_bundle(") + LuaString(SpoolStem) + TEXT(",") + LuaString(Content.PackageRoot) +
        TEXT("); if not b then error(tostring(e)) end ") + CacheEntry() + TEXT("={bundle=b,paths={},upload=") +
        LuaString(UploadId) + WorldFields + TEXT("}; return b");
    const TWeakPtr<FP4GamePreviewUpload> Weak = AsShared();
    Link->Eval(Source,
        [Weak, Attempt](const FP4GameReply& Reply)
        {
            const TSharedPtr<FP4GamePreviewUpload> Self = Weak.Pin();
            if (!Self || Self->bFinished)
                return;
            if (Reply.bTimedOut && Attempt < 2)
            {
                Self->BeginBundle(Attempt + 1);
                return;
            }
            if (!Reply.bOk || !Reply.Value || !Reply.Value->TryGetString(Self->Bundle) ||
                !Self->Bundle.StartsWith(TEXT("@asset_bundle:")))
            {
                Self->Finish(false,
                    Reply.bOk ? TEXT("The game returned an invalid preview bundle handle.") : Self->Failure(Reply));
                return;
            }
            Self->StageAliases();
        });
}

void FP4GamePreviewUpload::StageAliases()
{
    // Store every alias before mounting, so a lost mount reply can be recovered by
    // the next Play without recooking or relinquishing the mounted shader set.
    if (AliasIndex == Aliases.Num())
    {
        SendNextChunk();
        return;
    }
    FString Source = OwnEntry();
    while (AliasIndex < Aliases.Num() && Source.Len() < 12000)
    {
        const auto& Alias = Aliases[AliasIndex++];
        Source += TEXT("c.paths[") + LuaString(Alias.Key) + TEXT("]=") + LuaString(Alias.Value) + TEXT("; ");
    }
    Source += TEXT("return true");
    const TWeakPtr<FP4GamePreviewUpload> Weak = AsShared();
    Link->Eval(Source,
        [Weak](const FP4GameReply& Reply)
        {
            const TSharedPtr<FP4GamePreviewUpload> Self = Weak.Pin();
            if (!Self || Self->bFinished)
                return;
            if (!ReadBool(Reply))
            {
                Self->Finish(false, Self->Failure(Reply));
                return;
            }
            Self->StageAliases();
        });
}

void FP4GamePreviewUpload::SendNextChunk()
{
    if (PartIndex >= Parts.Num())
    {
        Mount();
        return;
    }
    const FPart& Part = Parts[PartIndex];
    if (!File)
    {
        File.Reset(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Part.Path));
        if (!File)
        {
            Finish(false, TEXT("Could not open the cooked preview file: ") + Part.Path);
            return;
        }
        Offset = 0;
    }
    if (Offset == Part.Size)
    {
        File.Reset();
        ++PartIndex;
        SendNextChunk();
        return;
    }
    Chunk.SetNumUninitialized(int32(FMath::Min<int64>(32768, Part.Size - Offset)));
    if (!File->Read(Chunk.GetData(), Chunk.Num()))
    {
        Finish(false, TEXT("Could not read the cooked preview file: ") + Part.Path);
        return;
    }
    Progress = FString::Printf(
        TEXT("Sending imported assets · %.1f / %.1f MiB"), double(Sent) / (1 << 20), double(Total) / (1 << 20));
    SendChunk();
}

void FP4GamePreviewUpload::SendChunk(int32 Attempt)
{
    const TCHAR* const PartNames[] = {TEXT("pak"), TEXT("utoc"), TEXT("ucas"), TEXT("shaders")};
    const TWeakPtr<FP4GamePreviewUpload> Weak = AsShared();
    Link->Call(TEXT("asset.write_bundle"),
        {FP4GameLink::Text(Bundle), FP4GameLink::Text(PartNames[Parts[PartIndex].Kind]),
            FP4GameLink::Text(FString::Printf(TEXT("%lld"), Offset)), FP4GameLink::Text(FBase64::Encode(Chunk))},
        [Weak, Attempt](const FP4GameReply& Reply)
        {
            const TSharedPtr<FP4GamePreviewUpload> Self = Weak.Pin();
            if (!Self || Self->bFinished)
                return;
            // The exact same bytes at the same offset are an idempotent retry.
            if (Reply.bTimedOut && Attempt < 2)
            {
                Self->SendChunk(Attempt + 1);
                return;
            }
            if (!Reply.bOk)
            {
                Self->Finish(false, Self->Failure(Reply));
                return;
            }
            Self->Offset += Self->Chunk.Num();
            Self->Sent += Self->Chunk.Num();
            Self->SendNextChunk();
        });
}

void FP4GamePreviewUpload::Mount(int32 Attempt)
{
    Progress = TEXT("Loading imported assets into the running game");
    const TWeakPtr<FP4GamePreviewUpload> Weak = AsShared();
    Link->Call(TEXT("asset.mount_bundle"), {FP4GameLink::Text(Bundle), FP4GameLink::Text(Content.ShaderMetadataJson)},
        [Weak, Attempt](const FP4GameReply& Reply)
        {
            const TSharedPtr<FP4GamePreviewUpload> Self = Weak.Pin();
            if (!Self || Self->bFinished)
                return;
            if (Reply.bTimedOut && Attempt < 2)
            {
                Self->Mount(Attempt + 1);
                return;
            }
            if (!Reply.bOk)
            {
                Self->ResolveMount(Self->Failure(Reply));
                return;
            }
            Self->bMounted = true;
            Self->Activate();
        });
}

void FP4GamePreviewUpload::ResolveMount(const FString& Why, int32 Attempt)
{
    const TWeakPtr<FP4GamePreviewUpload> Weak = AsShared();
    Link->Call(TEXT("asset.bundle_status"), {FP4GameLink::Text(Bundle)},
        [Weak, Why, Attempt](const FP4GameReply& Reply)
        {
            const TSharedPtr<FP4GamePreviewUpload> Self = Weak.Pin();
            if (!Self || Self->bFinished)
                return;
            if (Reply.bTimedOut && Attempt < 2)
            {
                Self->ResolveMount(Why, Attempt + 1);
                return;
            }
            const TSharedPtr<FJsonObject>* Status = nullptr;
            FString State;
            if (Reply.bOk && Reply.Value && Reply.Value->TryGetObject(Status))
                (*Status)->TryGetStringField(TEXT("state"), State);
            if (State == TEXT("mounted"))
            {
                Self->bMounted = true;
                Self->Activate();
                return;
            }
            // Unknown status is left in the Lua cache. Cleanup also checks status and
            // never releases a mounted bundle whose reply was lost.
            Self->Finish(false, Why);
        });
}

void FP4GamePreviewUpload::Activate(int32 Attempt)
{
    const FString Entry = bCached
        ? TEXT("local c=kadan_preview_bundles and ") + CacheEntry() + TEXT("; if not c or c.bundle~=handle(") +
            LuaString(Bundle) + TEXT(") then error('The cached preview bundle changed.') end ")
        : OwnEntry();
    FString Scene;
    if (!Plan.WorldSourceObjectPath.IsEmpty())
    {
        Progress = TEXT("Registering the editor world in the running game");
        Scene = TEXT("if not c.preview then error('The cached editor world has no reserved native area.') end ") TEXT(
            "local info,why=game.preview_scene(c.preview); if not info then error('Could not read the editor preview area: '..tostring(why)) end ")
            TEXT(
                "if not info.admitted or info.world_package~=c.world_package or info.scene_object~=c.scene_object then ")
                TEXT("if game.world()==info.world_name then return {needs_neutral=true} end ") TEXT(
                    "local admitted,reason=game.admit_preview(c.preview,c.bundle,c.world_package,c.scene_object); ")
                    TEXT(
                        "if not admitted then error('Could not admit the editor preview scene: '..tostring(reason)) end ")
                        TEXT("info=assert(game.preview_scene(c.preview),'The admitted preview area disappeared.') end ")
                            TEXT("c.preview_generation=info.generation; c.world_name=info.world_name; ");
    }
    const FString Source = Entry +
        TEXT(
            "local s,e=asset.bundle_status(c.bundle); if not s or s.state~='mounted' then error(tostring(e or 'Preview bundle is not mounted.')) end ") +
        Scene +
        TEXT(
            "kadan_preview.paths=kadan_preview.paths or {}; for a,b in pairs(c.paths) do kadan_preview.paths[a]=b end return {ready=true,world_name=c.world_name or ''}");
    const TWeakPtr<FP4GamePreviewUpload> Weak = AsShared();
    Link->Eval(Source,
        [Weak, Attempt](const FP4GameReply& Reply)
        {
            const TSharedPtr<FP4GamePreviewUpload> Self = Weak.Pin();
            if (!Self || Self->bFinished)
                return;
            if (Reply.bTimedOut && Attempt < 2)
            {
                Self->Activate(Attempt + 1);
                return;
            }
            const TSharedPtr<FJsonObject>* Status = nullptr;
            if (!Reply.bOk || !Reply.Value || !Reply.Value->TryGetObject(Status))
            {
                Self->Finish(
                    false, Reply.bOk ? TEXT("The preview asset aliases were not activated.") : Self->Failure(Reply));
                return;
            }
            bool bNeutral = false, bReady = false;
            (*Status)->TryGetBoolField(TEXT("needs_neutral"), bNeutral);
            if (bNeutral)
            {
                Self->bNeedsNeutralTravel = true;
                Self->Finish(false,
                    TEXT(
                        "This editor world's reserved area currently contains a different preview. Travel to a neutral game map before loading it."));
                return;
            }
            (*Status)->TryGetBoolField(TEXT("ready"), bReady);
            (*Status)->TryGetStringField(TEXT("world_name"), Self->ReservedWorld);
            Self->Finish(bReady, bReady ? FString() : TEXT("The preview asset aliases were not activated."));
        });
}

void FP4GamePreviewUpload::ReleaseUnfinished()
{
    if (!Link || !Link->IsOpen() || bMounted || Plan.Fingerprint.IsEmpty())
        return;
    const FString Source = TEXT("local c=kadan_preview_bundles and ") + CacheEntry() +
        TEXT("; if not c or c.upload~=") + LuaString(UploadId) +
        TEXT(
            " then return false end local ok,s=pcall(asset.bundle_status,c.bundle); if not ok or not s or s.state=='mounted' then return false end ")
            TEXT("local released=asset.release_bundle(c.bundle); if released then ") +
        CacheEntry() + TEXT("=nil end return released");
    Link->Eval(Source, [](const FP4GameReply&) {});
}

void FP4GamePreviewUpload::Finish(bool bOk, const FString& Error)
{
    if (bFinished)
        return;
    bFinished = true;
    File.Reset();
    Chunk.Reset();
    if (!bOk)
        ReleaseUnfinished();
    FDone Callback = MoveTemp(Done);
    if (Callback)
        Callback(bOk, bOk || Progress.IsEmpty() ? Error : Progress + TEXT(": ") + Error);
}

void FP4GamePreviewUpload::Cancel()
{
    if (bFinished)
        return;
    bFinished = true;
    Done = nullptr;
    File.Reset();
    Chunk.Reset();
    ReleaseUnfinished();
    Link = nullptr;
}
