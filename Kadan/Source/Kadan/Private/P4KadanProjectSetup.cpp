#include "P4KadanProjectSetup.h"
#include "P4GameNames.h"
#include "P4AuthoringRows.h"
#include "P4ExportProcess.h"
#include "Engine/Engine.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "NavigationData.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "P4GameBuild.h"

namespace
{
const FString CollisionSection(TEXT("[/Script/Engine.CollisionProfile]"));
const FString EngineSection(TEXT("[/Script/Engine.Engine]"));
// The game's renderer settings are the project's in its own editor too (forward shading, fixed exposure, shadow and
// reflection settings), so the editor draws a map as the game's editor did.
const FString RendererSection(TEXT("[/Script/Engine.RendererSettings]"));
// Kadan's plugin of the game's type declarations, made from the game's reflection.
const TCHAR* const DeclarationsPlugin = TEXT("P4Stubs");
bool ChangedThisSession = false;

FString ConfigKey(const FString& Line)
{
    FString Key, Value;
    if (!Line.Split(TEXT("="), &Key, &Value))
        return FString();
    Key = Key.TrimStartAndEnd();
    if (Key.StartsWith(TEXT("+")) || Key.StartsWith(TEXT("-")) || Key.StartsWith(TEXT("!")) ||
        Key.StartsWith(TEXT(".")))
        Key.RightChopInline(1);
    return Key;
}

// The class a section header such as [/Script/Module.Class] is for, when it is one this editor has.
const UClass* SectionClass(const FString& Header)
{
    if (!Header.StartsWith(TEXT("[/Script/")) || !Header.EndsWith(TEXT("]")))
        return nullptr;
    return FindObject<UClass>(nullptr, *Header.Mid(1, Header.Len() - 2));
}

// Whether the plugin of the game's type declarations is there but off in this project, which setup can change.
bool DeclarationsOff()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(DeclarationsPlugin);
    return Plugin.IsValid() && !Plugin->IsEnabled();
}

bool DeclarationsReady(FString& Why)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(DeclarationsPlugin);
    if (!Plugin.IsValid())
    {
        Why = TEXT("Kadan can't find its P4Stubs plugin, which tells Unreal the game's types. Close Unreal Editor, put "
                   "the P4Stubs folder from the Kadan zip beside the Kadan folder in your Plugins folder, then open the "
                   "project again.");
        return false;
    }
    if (!Plugin->IsEnabled())
    {
        Why = ChangedThisSession ? TEXT("P4Stubs is turned on for this project. Restart Unreal Editor to load it.")
                                 : TEXT("The P4Stubs plugin is turned off in this project. Setup turns it on.");
        return false;
    }
    const TArray<FString> Modules = P4KadanDeclarationModules();
    FString Missing;
    for (const FString& Module : Modules)
        if (!FModuleManager::Get().IsModuleLoaded(*Module))
            Missing = Module;
    if (!Modules.Num() || !Missing.IsEmpty())
    {
        Why = FString::Printf(TEXT("Unreal did not load the P4Stubs plugin's %s module. Check that the P4Stubs folder "
                                   "is the one from this Kadan zip, then restart Unreal Editor. The Output Log says "
                                   "why it was not loaded."),
            Modules.Num() ? *Missing : TEXT("game type"));
        return false;
    }
    for (TObjectIterator<UClass> Class; Class; ++Class)
        if (Class->IsChildOf(AWorldSettings::StaticClass()) && P4KadanDeclaredClass(*Class))
            return true;
    Why = TEXT(
        "The game world settings type is unavailable. Check this project's game type declarations and restart Unreal Editor.");
    return false;
}
} //namespace

TArray<FString> P4KadanDeclarationModules()
{
    TArray<FString> Modules;
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(DeclarationsPlugin);
    if (!Plugin.IsValid())
        return Modules;
    // The runtime modules declare the game's types; the plugin's editor module only supports them.
    for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
        if (Module.Type == EHostType::Runtime)
            Modules.Add(Module.Name.ToString());
    return Modules;
}

bool P4KadanDeclaredClass(const UClass* Class)
{
    if (!Class)
        return false;
    const FString Package = Class->GetOutermost()->GetName();
    for (const FString& Module : P4KadanDeclarationModules())
        if (Package == TEXT("/Script/") + Module)
            return true;
    return false;
}

bool P4KadanGameConfigClasses(const FString& GameIni, FP4KadanGameConfigClasses& Out, FString& Why)
{
    Out = FP4KadanGameConfigClasses();
    TArray<FString> Lines;
    GameIni.ParseIntoArrayLines(Lines, false);
    FString Section, NavigationSystem;
    int32 WorldClasses = 0, NavigationSystems = 0;
    for (const FString& Raw : Lines)
    {
        const FString Line = Raw.TrimStartAndEnd();
        if (Line.StartsWith(TEXT("[")))
        {
            Section = Line;
            continue;
        }
        FString Key, Value;
        if (Section != EngineSection || !Line.Split(TEXT("="), &Key, &Value))
            continue;
        Key = Key.TrimStartAndEnd();
        Value = Value.TrimStartAndEnd();
        if (Key == TEXT("WorldSettingsClassName"))
        {
            Out.WorldSettings = Value;
            ++WorldClasses;
        }
        if (Key == TEXT("NavigationSystemClassName"))
        {
            NavigationSystem = Value;
            ++NavigationSystems;
        }
    }
    const UClass* World = FindObject<UClass>(nullptr, *Out.WorldSettings);
    if (WorldClasses != 1 || NavigationSystems != 1 || !World || !World->IsChildOf(AWorldSettings::StaticClass()))
    {
        Why = TEXT("Choose the original Pikmin 4 game packages. Their engine settings are missing or incomplete.");
        return false;
    }
    for (const FString& Raw : Lines)
    {
        const FString Line = Raw.TrimStartAndEnd();
        if (!Line.StartsWith(TEXT("[")))
            continue;
        const UClass* Class = SectionClass(Line);
        const bool System = Line == TEXT("[") + NavigationSystem + TEXT("]");
        if (!System && !(Class && Class->IsChildOf(ANavigationData::StaticClass())))
            continue;
        if (Out.NavigationSections.Contains(Line))
        {
            Why = TEXT("The game configuration repeats a navigation section.");
            return false;
        }
        Out.NavigationSections.Add(Line);
    }
    return true;
}

bool P4KadanProjectConfig(
    const FString& GameIni, const FString& ProjectIni, FString& Updated, int32& Channels, int32& Profiles, FString& Why)
{
    Channels = Profiles = 0;
    if (GameIni.Len() > 1024 * 1024 || ProjectIni.Len() > 4 * 1024 * 1024)
    {
        Why = TEXT("The project or game configuration is too large.");
        return false;
    }
    FP4KadanGameConfigClasses Classes;
    if (!P4KadanGameConfigClasses(GameIni, Classes, Why))
        return false;
    const FString WorldClass = Classes.WorldSettings;
    TArray<FString> GameLines, ProjectLines, CollisionLines;
    TMap<FString, TArray<FString>> Navigation;
    GameIni.ParseIntoArrayLines(GameLines, false);
    ProjectIni.ParseIntoArrayLines(ProjectLines, false);
    int32 CollisionSections = 0;
    bool InCollision = false, Terrain = false;
    FString GameSection;
    TSet<FString> ChannelLines;
    for (const FString& Raw : GameLines)
    {
        const FString Line = Raw.TrimStartAndEnd();
        if (Line.StartsWith(TEXT("[")))
        {
            InCollision = Line == CollisionSection;
            GameSection = Line;
            if (Classes.NavigationSections.Contains(GameSection) || GameSection == RendererSection)
                Navigation.Add(GameSection);
            if (InCollision)
                ++CollisionSections;
            continue;
        }
        if (!InCollision)
        {
            if (TArray<FString>* Lines = Navigation.Find(GameSection))
                Lines->Add(Raw);
            continue;
        }
        if (Line.StartsWith(TEXT("+DefaultChannelResponses=")))
        {
            ++Channels;
            if (ChannelLines.Contains(Line))
            {
                Why = TEXT("The game configuration repeats a collision channel.");
                return false;
            }
            ChannelLines.Add(Line);
        }
        if (Line.StartsWith(TEXT("+Profiles=")))
        {
            ++Profiles;
            Terrain |= Line.Contains(TEXT("Name=\"CP_Terrain\""));
        }
        CollisionLines.Add(Raw);
    }
    if (CollisionSections != 1 || Channels != 18 || Profiles < 1 || !Terrain)
    {
        Why = TEXT("Choose the original Pikmin 4 game packages. Their collision setup is missing or incomplete.");
        return false;
    }

    // DrawOffset is an editor overlay preference, not a Recast bake setting.
    // The game's 1 cm offset clips through the visible cave terrain. Keep the
    // generation grid exact, but give a new editor overlay enough clearance.
    // Repair the previously copied game value while retaining creator choices.
    const FString RecastSection(TEXT("[/Script/NavigationSystem.RecastNavMesh]"));
    if (TArray<FString>* Base = Navigation.Find(RecastSection))
    {
        FString SourceOffset, CreatorOffset, ProjectSection;
        const auto Offset = [](const FString& Line, FString& Value)
        {
            FString Key;
            if (!Line.Split(TEXT("="), &Key, &Value))
                return false;
            float Number = 0.f;
            Value = Value.TrimStartAndEnd();
            return LexTryParseString(Number, *Value) && FMath::IsFinite(Number);
        };
        for (const FString& Line : *Base)
            if (ConfigKey(Line).Equals(TEXT("DrawOffset"), ESearchCase::IgnoreCase))
                Offset(Line, SourceOffset);
        for (const FString& Raw : ProjectLines)
        {
            const FString Line = Raw.TrimStartAndEnd();
            if (Line.StartsWith(TEXT("[")))
                ProjectSection = Line;
            if (ProjectSection == RecastSection && ConfigKey(Line).Equals(TEXT("DrawOffset"), ESearchCase::IgnoreCase))
            {
                FString Value;
                if (Offset(Line, Value) &&
                    (SourceOffset.IsEmpty() ||
                        !FMath::IsNearlyEqual(FCString::Atof(*Value), FCString::Atof(*SourceOffset))))
                    CreatorOffset = Value;
            }
        }
        const FString Display = TEXT("DrawOffset=") +
            (CreatorOffset.IsEmpty() ? FString(TEXT("20.000000")) : CreatorOffset);
        Base->RemoveAll(
            [](const FString& Line) { return ConfigKey(Line).Equals(TEXT("DrawOffset"), ESearchCase::IgnoreCase); });
        Base->Add(Display);
    }

    // Replace only the collision section, the confirmed native game navigation
    // sections, the game's renderer settings and WorldSettingsClassName. Preserve
    // unrelated creator settings, including creator renderer keys the game does not set.
    TArray<FString> Output;
    TMap<FString, TSet<FString>> NavigationKeys;
    TMap<FString, TArray<FString>> PreservedNavigation;
    for (const auto& Pair : Navigation)
        for (const FString& Line : Pair.Value)
        {
            const FString Key = ConfigKey(Line);
            if (!Key.IsEmpty())
                NavigationKeys.FindOrAdd(Pair.Key).Add(Key.ToLower());
        }
    FString Section;
    bool AddedWorldClass = false;
    for (const FString& Raw : ProjectLines)
    {
        const FString Line = Raw.TrimStartAndEnd();
        if (Line.StartsWith(TEXT("[")))
        {
            if (Section == EngineSection && !AddedWorldClass)
            {
                Output.Add(TEXT("WorldSettingsClassName=") + WorldClass);
                AddedWorldClass = true;
            }
            Section = Line;
        }
        if (Section == CollisionSection)
            continue;
        if (Navigation.Contains(Section))
        {
            if (!Line.IsEmpty() && !Line.StartsWith(TEXT("[")) && !Navigation.FindChecked(Section).Contains(Raw) &&
                !NavigationKeys.FindOrAdd(Section).Contains(ConfigKey(Line).ToLower()))
                PreservedNavigation.FindOrAdd(Section).Add(Raw);
            continue;
        }
        if (Section == EngineSection && ConfigKey(Line).Equals(TEXT("WorldSettingsClassName"), ESearchCase::IgnoreCase))
            continue;
        Output.Add(Raw);
    }
    if (!AddedWorldClass)
    {
        if (Section != EngineSection)
            Output.Add(EngineSection);
        Output.Add(TEXT("WorldSettingsClassName=") + WorldClass);
    }
    Output.Add(CollisionSection);
    Output.Append(CollisionLines);
    TArray<FString> Copied = Classes.NavigationSections;
    Copied.Add(RendererSection);
    for (const FString& Name : Copied)
    {
        if (const TArray<FString>* Lines = Navigation.Find(Name))
        {
            Output.Add(Name);
            Output.Append(*Lines);
            if (const TArray<FString>* Preserved = PreservedNavigation.Find(Name))
                Output.Append(*Preserved);
        }
    }
    Updated = FString::Join(Output, TEXT("\n")) + TEXT("\n");
    return true;
}

bool P4KadanSetupCanFinish()
{
    FString Ignored;
    return DeclarationsOff() || DeclarationsReady(Ignored);
}

bool P4KadanProjectRestartRequired()
{
    return ChangedThisSession;
}

bool P4KadanProjectReady(FString& Why)
{
    if (!DeclarationsReady(Why))
        return false;
    if (ChangedThisSession)
    {
        Why = TEXT("Restart Unreal Editor to use the game collision channels and world settings.");
        return false;
    }
    if (!GEngine || !GEngine->WorldSettingsClass || !P4KadanDeclaredClass(GEngine->WorldSettingsClass))
    {
        Why = TEXT("Choose Set up project on the Import page, then restart Unreal Editor before importing.");
        return false;
    }
    TArray<FString> Channels;
    FCollisionResponseTemplate Terrain;
    if (!GConfig ||
        !GConfig->GetArray(
            TEXT("/Script/Engine.CollisionProfile"), TEXT("DefaultChannelResponses"), Channels, GEngineIni) ||
        Channels.Num() != 18 || !UCollisionProfile::Get()->GetProfileTemplate(FName(TEXT("CP_Terrain")), Terrain))
    {
        Why = TEXT(
            "Choose Set up project on the Import page to add the game collision channels, then restart Unreal Editor before importing.");
        return false;
    }
    return true;
}

// Turns the declarations plugin on in the project file, as Unreal's Plugins window does. It loads in the next session.
static bool EnableDeclarations(FString& Why)
{
    FText Failure;
    if (!IProjectManager::Get().SetPluginEnabled(DeclarationsPlugin, true, Failure) ||
        !IProjectManager::Get().SaveCurrentProjectToDisk(Failure))
    {
        Why = TEXT("Kadan could not turn on the P4Stubs plugin: ") + Failure.ToString() +
            TEXT(" Turn on Kadan game types in Edit > Plugins, then restart Unreal Editor.");
        return false;
    }
    ChangedThisSession = true;
    return true;
}

FP4KadanProjectSetupResult P4KadanSetupProject(const FString& GamePaks, const FString& Repak)
{
    FP4KadanProjectSetupResult Result;
    // The game's settings name its types, so P4Stubs has to be loaded before they can be copied: when it is off, setup
    // turns it on and finishes after the restart.
    if (DeclarationsOff())
    {
        Result.Ok = EnableDeclarations(Result.Message);
        Result.RestartRequired = Result.Ok;
        if (Result.Ok)
            Result.Message = TEXT("Turned on the P4Stubs plugin. Restart Unreal Editor, then Kadan finishes the setup.");
        return Result;
    }
    if (!DeclarationsReady(Result.Message))
        return Result;
    const FString Pak = FPaths::Combine(GamePaks, P4GameContainerIn(GamePaks) + TEXT(".pak"));
    if (!IFileManager::Get().FileExists(*Pak))
    {
        Result.Message = TEXT("Choose the original game Paks folder, the one containing Carrot4-Switch.pak (Nintendo Switch) or pakchunk0-Sage.pak (Nintendo Switch 2 Edition).");
        return Result;
    }
    TArray<uint8> Bytes;
    FString Why;
    if (!P4RunExportTool(Repak, {TEXT("get"), Pak, TEXT("Carrot4/Config/DefaultEngine.ini")}, 60.0, Bytes, Why))
    {
        Result.Message = TEXT(
            "Could not read the game configuration. Check the repak program and original game Paks folder.");
        return Result;
    }
    if (!Bytes.Num() || Bytes.Num() > 1024 * 1024 || Bytes.Contains(0))
    {
        Result.Message = TEXT("The game configuration is empty or has an unexpected format.");
        return Result;
    }
    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
    FString GameIni(Converted.Length(), Converted.Get());
    if (GameIni.Len() && GameIni[0] == 0xfeff)
        GameIni.RightChopInline(1);
    const FString Config = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
    FString Before, After;
    if (IFileManager::Get().FileExists(*Config) && !FFileHelper::LoadFileToString(Before, *Config))
    {
        Result.Message = TEXT("The project configuration could not be read.");
        return Result;
    }
    if (!P4KadanProjectConfig(GameIni, Before, After, Result.Channels, Result.Profiles, Result.Message))
        return Result;
    if (Before != After)
    {
        if (IFileManager::Get().IsReadOnly(*Config))
        {
            Result.Message = TEXT("Make the project configuration writable before setting up Kadan.");
            return Result;
        }
        const FString Temporary = Config + TEXT(".kadan-setup-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        if (!IFileManager::Get().MakeDirectory(*FPaths::ProjectConfigDir(), true) ||
            !FFileHelper::SaveStringToFile(After, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            Result.Message = TEXT("The project configuration could not be written.");
            return Result;
        }
        FString Current;
        const bool StillSame = !IFileManager::Get().FileExists(*Config)
            ? Before.IsEmpty()
            : FFileHelper::LoadFileToString(Current, *Config) && Current == Before;
        if (!StillSame || !IFileManager::Get().Move(*Config, *Temporary, true, false))
        {
            IFileManager::Get().Delete(*Temporary);
            Result.Message = TEXT(
                "The project configuration changed or could not be saved. Its existing settings were preserved.");
            return Result;
        }
        ChangedThisSession = true;
    }
    Result.Ok = true;
    Result.RestartRequired = ChangedThisSession || !P4KadanProjectReady(Why);
    Result.Message = Result.RestartRequired
        ? TEXT("Project setup is saved. Restart Unreal Editor before importing game content.")
        : TEXT("This project already uses the game collision channels and world settings.");
    return Result;
}

FString UP4AuthoringTools::NativeSetupKadanProject(const FString& GamePaks, const FString& Repak)
{
    const FP4KadanProjectSetupResult Result = P4KadanSetupProject(GamePaks, Repak);
    const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("Ok"), Result.Ok);
    Json->SetBoolField(TEXT("RestartRequired"), Result.RestartRequired);
    Json->SetNumberField(TEXT("Channels"), Result.Channels);
    Json->SetNumberField(TEXT("Profiles"), Result.Profiles);
    Json->SetStringField(TEXT("Message"), Result.Message);
    FString Text;
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
    return Text;
}

bool UP4AuthoringTools::NativeKadanProjectReady()
{
    FString Why;
    return P4KadanProjectReady(Why);
}
