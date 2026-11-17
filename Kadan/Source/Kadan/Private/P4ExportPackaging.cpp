// The end of a mod export: the manifest, the container, the prepared files and their publication.

#include "P4ExportInternal.h"
#include "P4GameBuild.h"
#include "P4GameNames.h"

using namespace P4Export;

bool FP4ExportRun::WriteManifest()
{
    // Without Tsuchi there is no manifest: the container goes into the game's own Paks folder, which the game
    // mounts at start. Only the shader library still needs a home the game opens by itself.
    if (Request.Native)
    {
        if (Materials->HasMaterials())
        {
            if (Request.Tools.Repak.IsEmpty() || !IFileManager::Get().FileExists(*Request.Tools.Repak))
                return Fail(TEXT(
                    "Kadan needs repak to put your materials' shaders into the mod. Set the repak path in Kadan's settings."));
            if (!Materials->ShaderLibrary(ShaderLibrary, Error))
                return Fail(Error);
            Result.Told.Add(TEXT("Your materials' shaders go into the Wwise plugin's shader library, which the game "
                                 "opens by itself. Only one mod cooked without Tsuchi can carry shaders at a time."));
        }
        if (!Materials->Release(Error))
            return Fail(Error);
        if (Materials->HasDirectionalLightFunctions())
            return Fail(TEXT("A directional light function needs Tsuchi, which keeps a shadow channel for it. Export "
                             "this mod for Tsuchi, or use the light function on a spot or point light."));
        Metadata = Prepared / TEXT("romfs/" P4_GAME_PROJECT "/Content/Paks");
        Result.Told.Add(TEXT("The mod is cooked for the game without Tsuchi: copy its romfs folder over the game's. "
                             "Edited levels keep the navigation they hold."));
        return true;
    }
    // The library holding Kadan's own shader maps for the mod's materials goes into the mod's .pak, where Tsuchi
    // opens it when it mounts the mod.
    if (Materials->HasMaterials())
    {
        if (Request.Tools.Repak.IsEmpty() || !IFileManager::Get().FileExists(*Request.Tools.Repak))
            return Fail(TEXT(
                "Kadan needs repak to put your materials' shaders into the mod. Set the repak path in Kadan's settings."));
        if (!Materials->ShaderLibrary(ShaderLibrary, Error))
            return Fail(Error);
    }
    if (!Materials->Release(Error))
        return Fail(Error);
    if (Preview.GameEditCandidates.Num() || !Maps.empty())
    {
        // A map shipped whole says nothing here unless its navigation was built again.
        auto Listed = Maps;
        if (WholeLevels.Num())
            Listed.erase(std::remove_if(Listed.begin(), Listed.end(),
                             [](const P4MapKit::Export::MapEdit& Map)
                             { return Map.Changes.empty() && Map.Levels.empty() && Map.Placements.empty() && !Map.Navigation; }),
                Listed.end());
        if (!P4MapKit::Export::FrameworkFiles(Mod, Listed, Neutral, Why))
            return Fail(UTF8_TO_TCHAR(Why.c_str()));
        for (int32 Index = 0; Index < int32(Maps.size()); ++Index)
        {
            const FString MapName = FPackageName::GetShortName(UTF8_TO_TCHAR(Maps[Index].Package.c_str()));
            const FMapNavigationEdit* Each = Navigations.IsValidIndex(Index) && Navigations[Index].Built
                ? &Navigations[Index]
                : nullptr;
            if (Each && Maps[Index].Navigation)
            {
                Result.Told.Add(FString::Printf(
                    TEXT(
                        "%s in %s changes where Pikmin can walk, so the navigation of %s was built again for the mod."),
                    *Each->Why, *Each->Level, *MapName));
                Result.Told.Append(Each->Navigation.Told);
            }
            else if (Each && Each->Navigation.Kept)
                Result.Told.Append(Each->Navigation.Told);
            else
                Result.Told.Add(FString::Printf(
                    TEXT("Nothing you changed is walked on or stands in the way, so %s keeps the game's navigation."),
                    *MapName));
        }
        for (const FP4GameRowEdit& Row : Result.GameRowEdits)
            Result.Told.Add(FString::Printf(
                TEXT(
                    "%s is %s by a change to row %llu of the game's placement list %s, because the game builds it from that row."),
                *Row.Actor, Row.Removed ? TEXT("removed") : TEXT("moved"), Row.Id,
                *FPackageName::GetShortName(Row.List)));
        if (Result.RebuiltLighting.Num())
            Result.Told.Add(
                TEXT("The edited map's lighting was rebuilt and is included at the game's lighting paths."));
        if (Result.BakedActors.Num())
            Result.Told.Add(Result.BakedActors.Num() == 1
                    ? FString::Printf(TEXT("%s has baked lighting from the game's own lights."), *Result.BakedActors[0])
                    : FString::Printf(
                          TEXT("%d of your added static meshes have baked lighting from the game's own lights."),
                          Result.BakedActors.Num()));
        for (const auto& List : PlacementLists)
            if (List->Rows.Num())
            {
                const FP4PlacementList& Placements = *List;
                int32 Changed = 0;
                for (const FP4PlacementRow& Row : Placements.Rows)
                    Changed += Row.Blobs.Num() ? 1 : 0;
                Result.Told.Add(FString::Printf(
                    TEXT(
                        "%d gameplay objects are in the placement list %s, and the game saves what becomes of them. %d of them have options you changed."),
                    Placements.Rows.Num(), *Placements.Package, Changed));
            }
    }
    else
        Neutral["mod.json"] = "{\"id\":" + P4MapKit::Export::Quote(Mod.Id) +
            ",\"name\":" + P4MapKit::Export::Quote(Mod.Name) + ",\"version\":" + P4MapKit::Export::Quote(Mod.Version) +
            ",\"framework\":" + std::to_string(P4MapKit::Export::MinimumFrameworkApi) + "}";
    if (Tables->Values.Num() || Content->Values.Num())
    {
        TSharedPtr<FJsonObject> Manifest;
        FString ManifestText = UTF8_TO_TCHAR(Neutral["mod.json"].c_str());
        if (!FJsonSerializer::Deserialize(
                TJsonReaderFactory<>::Create(ManifestText), Manifest, FJsonSerializer::EFlags::StoreNumbersAsStrings))
            return Fail(TEXT("The mod manifest could not be read."));
        if (Tables->Values.Num())
            Manifest->SetObjectField(TEXT("tables"), Tables);
        if (Content->Values.Num())
        {
            auto ExistingContent = Manifest->HasTypedField<EJson::Object>(TEXT("content"))
                ? Manifest->GetObjectField(TEXT("content"))
                : MakeShared<FJsonObject>();
            for (const auto& Group : Content->Values)
            {
                if (ExistingContent->HasField(Group.Key))
                    return Fail(TEXT("Two export declarations write the same content group."));
                ExistingContent->SetField(Group.Key, Group.Value);
            }
            Manifest->SetObjectField(TEXT("content"), ExistingContent);
        }
        ManifestText = P4TableManifestJson(Manifest);
        if (ManifestText.IsEmpty())
            return Fail(TEXT("The table changes could not be written."));
        Neutral["mod.json"] = TCHAR_TO_UTF8(*ManifestText);
        if (Tables->Values.Num())
            Result.Told.Add(TEXT("The manifest contains your table changes."));
        if (Content->Values.Num())
            Result.Told.Add(TEXT("The manifest declares your new treasures and creatures."));
    }
    if (Materials->HasDirectionalLightFunctions())
    {
        std::string Script;
        if (!P4MapKit::Export::DirectionalLightFunctionRuntime(Mod, Maps, Script, Why))
            return Fail(UTF8_TO_TCHAR(Why.c_str()));
        Neutral["main.lua"] = MoveTemp(Script);
        Result.Told.Add(TEXT("Directional light functions reserve a shadow channel while this mod's maps are open."));
    }
    Metadata = Prepared / TEXT("romfs/tsuchi/mods") / Request.Mod.Id;
    if (Sounds.Num())
    {
        if (!P4ExportCreatorSoundBank(Request, Sounds, Metadata, Result, Error))
            return Fail(Error);
        TSharedPtr<FJsonObject> Manifest;
        const FString Before = UTF8_TO_TCHAR(Neutral["mod.json"].c_str());
        if (!FJsonSerializer::Deserialize(
                TJsonReaderFactory<>::Create(Before), Manifest, FJsonSerializer::EFlags::StoreNumbersAsStrings) ||
            !Manifest)
            return Fail(TEXT("The mod's sound bank manifest could not be written."));
        TSharedPtr<FJsonObject> SoundContent;
        const TSharedPtr<FJsonObject>* Existing = nullptr;
        if (Manifest->HasField(TEXT("content")))
        {
            if (!Manifest->TryGetObjectField(TEXT("content"), Existing) || !Existing || !*Existing)
                return Fail(TEXT("The mod's content declarations are not an object."));
            SoundContent = *Existing;
        }
        else
        {
            SoundContent = MakeShared<FJsonObject>();
            Manifest->SetObjectField(TEXT("content"), SoundContent);
        }
        if (SoundContent->HasField(TEXT("sound_banks")))
            return Fail(TEXT("This mod already declares a sound bank."));
        auto Banks = MakeShared<FJsonObject>();
        Banks->SetStringField(TEXT("sounds"), TEXT("sounds.bnk"));
        SoundContent->SetObjectField(TEXT("sound_banks"), Banks);
        const FString Text = P4TableManifestJson(Manifest);
        if (Text.IsEmpty())
            return Fail(TEXT("The mod's sound bank manifest could not be written."));
        Neutral["mod.json"] = TCHAR_TO_UTF8(*Text);
    }
    return true;
}

bool FP4ExportRun::PackContainer()
{
    TArray<FString> Shipped = Own;
    Shipped.Append(WholeLevels);
    for (const FString& Package : SoundSources)
        Shipped.Remove(Package);
    for (const FString& Level : Result.Levels)
        Shipped.AddUnique(Level);
    for (const FString& LightingPackage : Result.RebuiltLighting)
        Shipped.AddUnique(LightingPackage);
    for (const FString& LightingPackage : Result.BakedLighting)
        Shipped.AddUnique(LightingPackage);
    for (const auto& List : PlacementLists)
        if (List->Asset.IsValid())
        {
            Shipped.Add(List->Package);
            Shipped.Add(List->ListPackage);
        }
    if (Request.PreparePreviewPackages)
    {
        TArray<FString> Extra;
        if (!Request.PreparePreviewPackages(Stage, Extra, Error))
            return Fail(Error);
        for (const FString& Package : Extra)
        {
            if (!Package.StartsWith(Names.PackageRoot + TEXT("/"), ESearchCase::CaseSensitive) ||
                !FPackageName::IsValidLongPackageName(Package) || Shipped.Contains(Package))
                return Fail(TEXT("Private preview preparation named an occupied or external package."));
            Shipped.Add(Package);
        }
    }
    Shipped.Sort();
    if (Shipped.Num())
    {
        // Tsuchi mounts only the active mod's container beside its manifest.
        if (!P4ReportExportStage(EP4ExportStage::Packing, Request.Mod.Id, Error))
        {
            return Fail(Error);
        }
        const FString Paks = Metadata;
        const FString Utoc = Paks / (Names.Container + TEXT("_P.utoc"));
        TArray<uint8> Output;
        // With the game's containers as reference, a package's dependency on a game package names the game package's
        // own export bundle, as the game's own packing does (tools/retoc).
        TArray<FString> Pack = {TEXT("to-zen"), Stage, Utoc, TEXT("--version"), P4GameEngineFor(Request.GamePaksDirectory)};
        if (!Request.GamePaksDirectory.IsEmpty())
            Pack.Append({TEXT("--reference"), Request.GamePaksDirectory});
        if (!IFileManager::Get().MakeDirectory(*Paks, true) ||
            !P4RunExportTool(Request.Tools.Retoc, Pack, Request.Tools.TimeoutSeconds, Output, Error))
            return Fail(Error);
        for (const TCHAR* Suffix : {TEXT(".pak"), TEXT(".ucas"), TEXT(".utoc")})
            if (IFileManager::Get().FileSize(*(Paks / (Names.Container + TEXT("_P") + Suffix))) <= 0)
                return Fail(TEXT("Pack did not produce a complete container"));
        if (ShaderLibrary.Num())
        {
            FP4GameBuildInfo Build;
            FString BuildError;
            if (!P4ResolveGameBuild(Request.GamePaksDirectory, Build, BuildError))
                return Fail(BuildError);
            if (!Build.Family->KadanShaders)
                return Fail(TEXT("This mod has materials with shaders of their own, and Kadan cannot compile shaders "
                                 "for this game build yet. Use the game's own materials and their instances for it."));
            // The container's .pak holds the library at tsuchi/mods/<mod>/, the only file in it. Without Tsuchi it
            // is the Wwise plugin's library: the engine opens each enabled plugin's library by itself.
            const FString PakFiles = Working / TEXT("pak");
            const FString Library = TEXT("kadan_") + Names.Container.Right(16);
            const FString Format = UTF8_TO_TCHAR(Build.Family->ShaderFormat);
            const FString LibraryFile = Request.Native
                ? PakFiles / TEXT(P4_GAME_PROJECT "/Plugins/Wwise/Content") /
                    (TEXT("ShaderArchive-Wwise-") + Format + TEXT(".ushaderbytecode"))
                : PakFiles / TEXT("tsuchi/mods") / Request.Mod.Id /
                    (TEXT("ShaderArchive-") + Library + TEXT("-") + Format + TEXT(".ushaderbytecode"));
            if (!NoLinks(PakFiles, Error) ||
                (IFileManager::Get().DirectoryExists(*PakFiles) &&
                    !IFileManager::Get().DeleteDirectory(*PakFiles, false, true)) ||
                !IFileManager::Get().MakeDirectory(*FPaths::GetPath(LibraryFile), true) ||
                !FFileHelper::SaveArrayToFile(ShaderLibrary, *LibraryFile))
                return Fail(Error.IsEmpty() ? TEXT("The shader library could not be staged.") : Error);
            const FString Pak = Paks / (Names.Container + TEXT("_P.pak"));
            if (!P4RunExportTool(Request.Tools.Repak,
                    {TEXT("pack"), TEXT("-q"), TEXT("--version"), TEXT("V11"), TEXT("-m"), TEXT("../../../"), PakFiles,
                        Pak},
                    Request.Tools.TimeoutSeconds, Output, Error))
                return Fail(Error);
            if (IFileManager::Get().FileSize(*Pak) <= int64(ShaderLibrary.Num()))
                return Fail(TEXT("The mod's .pak does not hold its shader library."));
        }
        if (!P4ReportExportStage(EP4ExportStage::Checking, Request.Mod.Id, Error))
        {
            return Fail(Error);
        }
        FP4ExportContainerCensus Census;
        if (!P4InspectExportContainer(Request.Tools, Utoc, Working, Inventory, Census, Error))
            return Fail(Error);
        // A package at a game path is in the container only as rebuilt lighting or as the creator's replacement.
        // The pack names such a package in its redirects to itself, the one redirect it may have.
        TSet<uint64> LightingIds, ReplacementIds;
        for (const FString& Package : Result.RebuiltLighting)
            LightingIds.Add(P4ExportPackageId(Package));
        for (const FString& Package : Result.Replacements)
            ReplacementIds.Add(P4ExportPackageId(Package));
        for (uint64 Id : Census.GameClaims)
            if (!LightingIds.Contains(Id) && !ReplacementIds.Contains(Id))
                return Fail(TEXT("Final container contains or redirects a game package"));
        for (const auto& Redirect : Census.Redirects)
            if ((Inventory.PackageIds().Contains(Redirect.Key) || Inventory.PackageIds().Contains(Redirect.Value)) &&
                !(Redirect.Key == Redirect.Value &&
                    (ReplacementIds.Contains(Redirect.Key) || LightingIds.Contains(Redirect.Key))))
                return Fail(FString::Printf(
                    TEXT(
                        "Final container redirects a game package (%016llx to %016llx) instead of holding freshly rebuilt lighting."),
                    Redirect.Key, Redirect.Value));
        for (const auto& Redirect : Census.CultureRedirects)
            if (Inventory.PackageIds().Contains(Redirect.Key) || Inventory.PackageIds().Contains(Redirect.Value))
                return Fail(
                    TEXT("Final container redirects a game package instead of holding freshly rebuilt lighting."));
        // The container holds exactly the packages cooked for this export.
        if (!P4ValidateExportCensusSelection(Census, Shipped, Error))
            return Fail(Error);
        for (const FString& Package : Shipped)
            if (!Census.PackageIds.Contains(P4ExportPackageId(Package)))
                return Fail(TEXT("Final container package identity differs from the selected cook"));
        if (Textures.Num())
        {
            const FString Readback = Working / TEXT("readback");
            if (!NoLinks(Readback, Error) ||
                (IFileManager::Get().DirectoryExists(*Readback) &&
                    !IFileManager::Get().DeleteDirectory(*Readback, false, true)))
                return Fail(Error.IsEmpty() ? TEXT("Cannot reset this export's readback directory") : Error);
            TArray<FString> TextureFilters;
            for (UTexture* Texture : Textures)
            {
                // retoc uses case-sensitive substring filters. A complete
                // mounted header path selects only this packed texture; other
                // packages are not part of the pixel-content readback check.
                TextureFilters.AddUnique(
                    TEXT("../../../") + P4ContainerRelativePath(Texture->GetOutermost()->GetName()) + TEXT(".uasset"));
            }
            if (!P4ExtractExportReadback(
                    Request.Tools, Utoc, Request.GamePaksDirectory, Working, Readback, TextureFilters, Error))
                return Fail(Error);
            for (UTexture* Texture : Textures)
            {
                const FString Relative = P4ContainerRelativePath(Texture->GetOutermost()->GetName()) + TEXT(".uasset");
                if (!P4VerifyCookedTextureContent(Stage / Relative, Readback / Relative, Error))
                    return Fail(Error);
            }
        }
        Result.Packages = Shipped;
    }
    return true;
}

bool FP4ExportRun::WritePreparedFiles()
{
    if (Request.Native)
    {
        // Only the container: the game's Paks folder gets nothing else.
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *Prepared, TEXT("*"), true, false);
        const FString Expected = TEXT("romfs/" P4_GAME_PROJECT "/Content/Paks/") + Names.Container + TEXT("_P.");
        for (FString File : Files)
        {
            if (!FPaths::MakePathRelativeTo(File, *(Prepared + TEXT("/"))) || !File.StartsWith(Expected) ||
                !(File.EndsWith(TEXT(".pak")) || File.EndsWith(TEXT(".utoc")) || File.EndsWith(TEXT(".ucas"))))
                return Fail(TEXT("Prepared export contains a file outside the game's Paks folder, ") + File + TEXT("."));
            Result.Files.Add(File);
        }
        Result.Files.Sort();
        return true;
    }
    for (const auto& File : Neutral)
    {
        const FString Relative = UTF8_TO_TCHAR(File.first.c_str());
        const FString Path = Metadata / Relative;
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true) ||
            !FFileHelper::SaveStringToFile(
                UTF8_TO_TCHAR(File.second.c_str()), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            return Fail(TEXT("Cannot write prepared framework files"));
    }
    for (int32 Index = 0; Index < Navigations.Num(); ++Index)
    {
        if (!Navigations[Index].Built || !Maps[Index].Navigation)
            continue;
        // The file goes where the manifest's map entry says, so the two always agree.
        const FString NavigationPackage = Groups[Index].Map;
        FString Relative;
        if (!P4MapNavigationPath(UTF8_TO_TCHAR(Neutral["mod.json"].c_str()), NavigationPackage, Relative))
            return Fail(TEXT("The manifest does not name the map's navigation file"));
        if (!P4WriteMapNavigation(Metadata, Relative, Navigations[Index].Navigation.Bytes, Error))
            return Fail(Error);
        Result.Told.Add(TEXT("The navigation file of ") + FPackageName::GetShortName(NavigationPackage) + TEXT(" is ") +
            Relative + TEXT("."));
    }
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Prepared, TEXT("*"), true, false);
    for (FString File : Files)
    {
        if (!FPaths::MakePathRelativeTo(File, *(Prepared + TEXT("/"))) ||
            !P4MapKit::Export::AllowedLightFunctionFile(
                Mod, TCHAR_TO_UTF8(*File), Materials->HasDirectionalLightFunctions(), Why))
            return Fail(TEXT("Prepared export contains a file outside its contract, ") + File + TEXT(". ") +
                UTF8_TO_TCHAR(Why.c_str()));
        Result.Files.Add(File);
    }
    Result.Files.Sort();
    return true;
}

bool FP4ExportRun::Publish()
{
    if (!NoLinks(Prepared, Error) || !NoLinks(Target, Error))
        return Fail(Error);
    const bool HadTarget = IFileManager::Get().DirectoryExists(*Target);
    if (IFileManager::Get().FileExists(*Target) && !HadTarget)
        return Fail(TEXT("Output target is a file"));
    // The prior export of this mod is replaced whichever way it was cooked: for Tsuchi (its manifest names the mod)
    // or for the game without Tsuchi (only this mod's container in the game's Paks folder).
    const bool PriorNative = HadTarget &&
        !IFileManager::Get().FileExists(*(Target / TEXT("romfs/tsuchi/mods") / Request.Mod.Id / TEXT("mod.json")));
    if (PriorNative)
    {
        TArray<FString> PriorFiles;
        IFileManager::Get().FindFilesRecursive(PriorFiles, *Target, TEXT("*"), true, false);
        const FString Expected = TEXT("romfs/" P4_GAME_PROJECT "/Content/Paks/") + Names.Container + TEXT("_P.");
        for (FString File : PriorFiles)
            if (!FPaths::MakePathRelativeTo(File, *(Target + TEXT("/"))) || !File.StartsWith(Expected))
                return Fail(TEXT("Existing output is not an export of this mod identity"));
    }
    else if (HadTarget)
    {
        FString Text;
        TSharedPtr<FJsonObject> Existing;
        FString Id;
        const FString Manifest = Target / TEXT("romfs/tsuchi/mods") / Request.Mod.Id / TEXT("mod.json");
        if (!FFileHelper::LoadFileToString(Text, *Manifest) || !P4ParseJson(Text, Existing) ||
            !Existing->TryGetStringField(TEXT("id"), Id) || !P4ExactNativeString(Id, Request.Mod.Id))
            return Fail(TEXT("Existing output is not an export of this mod identity"));
        TArray<FString> PriorFiles;
        IFileManager::Get().FindFilesRecursive(PriorFiles, *Target, TEXT("*"), true, false);
        // Permit replacement/removal of our exact prior generated runtime, even
        // when the new selection no longer contains a directional light function.
        // A creator's unrelated or edited script remains outside the contract.
        bool PriorLightFunctionRuntime = false;
        const FString PriorRuntime = FPaths::GetPath(Manifest) / TEXT("main.lua");
        if (IFileManager::Get().FileExists(*PriorRuntime))
        {
            std::vector<P4MapKit::Export::MapEdit> PriorMaps;
            const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
            if (Existing->TryGetArrayField(TEXT("maps"), Entries))
                for (const auto& Entry : *Entries)
                {
                    FString Map;
                    if (!Entry.IsValid() || Entry->Type != EJson::Object ||
                        !Entry->AsObject()->TryGetStringField(TEXT("map"), Map))
                        return Fail(TEXT("Existing output has an invalid map declaration."));
                    PriorMaps.emplace_back();
                    PriorMaps.back().Package = TCHAR_TO_UTF8(*Map);
                }
            std::string Expected;
            FString Actual;
            PriorLightFunctionRuntime = P4MapKit::Export::DirectionalLightFunctionRuntime(
                                            Mod, PriorMaps, Expected, Why) &&
                FFileHelper::LoadFileToString(Actual, *PriorRuntime) && Actual == UTF8_TO_TCHAR(Expected.c_str());
        }
        for (FString File : PriorFiles)
            if (!FPaths::MakePathRelativeTo(File, *(Target + TEXT("/"))) ||
                !P4MapKit::Export::AllowedLightFunctionFile(Mod, TCHAR_TO_UTF8(*File), PriorLightFunctionRuntime, Why))
                return Fail(TEXT("Existing output contains unrelated files, so it cannot be replaced"));
    }
    if (!P4ReportExportStage(EP4ExportStage::Publishing, Request.Mod.Id, Error))
    {
        return Fail(Error);
    }
    if (!P4PublishPreparedExport(Prepared, Target, HadTarget, Result.Told, Error))
    {
        return Fail(Error);
    }
    Result.Published = true;
    Result.Directory = Target;
    Out = MoveTemp(Result);
    Error.Reset();
    return true;
}
