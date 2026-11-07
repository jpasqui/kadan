#include "P4KadanProjectSetup.h"
#include "P4ExportProcess.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "Misc/AutomationTest.h"
#include "GameFramework/WorldSettings.h"
#include "Modules/ModuleManager.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanProjectConfig, "P4MapKit.Kadan.ProjectConfig",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKadanProjectConfig::RunTest(const FString&)
{
    FString Game = TEXT("[/Script/Engine.Engine]\nWorldSettingsClassName=/Script/Carrot4.MyWorldSettings\n")
        TEXT("NavigationSystemClassName=/Script/Carrot4.CarrotNavigationSystem\n[/Script/Engine.CollisionProfile]\n");
    for (int32 Index = 1; Index <= 18; ++Index)
        Game += FString::Printf(
            TEXT("+DefaultChannelResponses=(Channel=ECC_GameTraceChannel%d,Name=\"Game%d\")\n"), Index, Index);
    Game += TEXT("+Profiles=(Name=\"CP_Terrain\",CollisionEnabled=QueryAndPhysics)\n") TEXT(
        "[/Script/Carrot4.CarrotNavigationSystem]\nDefaultAgentName=Pikmin\n+SupportedAgents=ActualUserPakAgent\n")
        TEXT("[/Script/Carrot4.RecastNavMeshPikmin]\nCellSize=5.f\n") TEXT(
            "[/Script/NavigationSystem.RecastNavMesh]\nRegionPartitioning=ChunkyMonotone\nDefaultMaxSearchNodes=8192\n[UnrelatedGameSetting]\nPrivate=DoNotCopy\n");
    const FString Project = TEXT("; creator settings\n[/Script/Engine.RendererSettings]\nr.Custom=1\n") TEXT(
        "[/Script/Engine.CollisionProfile]\n+Profiles=(Name=\"Wrong\")\n")
        TEXT("[/Script/Carrot4.RecastNavMeshPikmin]\nCellSize=Wrong\nCreatorSetting=Keep\n") TEXT(
            "[/Script/NavigationSystem.RecastNavMesh]\nRegionPartitioning=Wrong\nDefaultMaxSearchNodes=1\nCreatorBaseSetting=Keep\n")
            TEXT(
                "[/Script/Engine.Engine]\nWorldSettingsClassName=/Script/Engine.WorldSettings\nGameViewportClientClassName=KeepMe\n")
                TEXT("[/Script/Engine.Engine]\nWorldSettingsClassName = OldDuplicate\n[Other]\nKeep=Yes\n");
    FString Updated, Again, Why;
    int32 Channels = 0, Profiles = 0;
    TestTrue(TEXT("User source configuration accepted"),
        P4KadanProjectConfig(Game, Project, Updated, Channels, Profiles, Why));
    TestEqual(TEXT("All original channels"), Channels, 18);
    TestEqual(TEXT("Original profiles"), Profiles, 1);
    TestTrue(TEXT("Creator renderer settings preserved"), Updated.Contains(TEXT("r.Custom=1")));
    TestTrue(TEXT("Unrelated engine key preserved"), Updated.Contains(TEXT("GameViewportClientClassName=KeepMe")));
    TestTrue(TEXT("Unrelated section preserved"), Updated.Contains(TEXT("[Other]\nKeep=Yes")));
    TestFalse(TEXT("No unrelated game settings copied"), Updated.Contains(TEXT("Private=DoNotCopy")));
    TestFalse(TEXT("Old collision removed"), Updated.Contains(TEXT("Name=\"Wrong\"")));
    TestFalse(TEXT("Duplicate old world settings removed"), Updated.Contains(TEXT("OldDuplicate")));
    TestTrue(TEXT("Actual world settings set"),
        Updated.Contains(TEXT("WorldSettingsClassName=/Script/Carrot4.MyWorldSettings")));
    TestTrue(TEXT("Native navigation defaults come from the user source"),
        Updated.Contains(TEXT("+SupportedAgents=ActualUserPakAgent")));
    TestTrue(TEXT("Native mesh settings come from the user source"), Updated.Contains(TEXT("CellSize=5.f")));
    TestTrue(TEXT("Unrelated creator navigation key preserved"), Updated.Contains(TEXT("CreatorSetting=Keep")));
    TestFalse(TEXT("Game navigation key replaced"), Updated.Contains(TEXT("CellSize=Wrong")));
    TestTrue(TEXT("Game base Recast generation settings reach native subclasses"),
        Updated.Contains(TEXT("RegionPartitioning=ChunkyMonotone")) &&
            Updated.Contains(TEXT("DefaultMaxSearchNodes=8192")));
    TestFalse(TEXT("Old base Recast generation settings are replaced"),
        Updated.Contains(TEXT("RegionPartitioning=Wrong")) || Updated.Contains(TEXT("DefaultMaxSearchNodes=1\n")));
    TestTrue(TEXT("Unrelated creator base Recast settings are preserved"),
        Updated.Contains(TEXT("CreatorBaseSetting=Keep")));
    TestTrue(TEXT("Repeat setup accepted"), P4KadanProjectConfig(Game, Updated, Again, Channels, Profiles, Why));
    TestEqual(TEXT("Repeat setup is stable"), Again, Updated);
    // The game's renderer settings replace the creator's same keys and keep the creator's others.
    const FString Renderer = Game + TEXT("[/Script/Engine.RendererSettings]\nr.ForwardShading=True\nr.Custom=2\n");
    FString Rendered, RenderedAgain;
    TestTrue(TEXT("Game renderer settings accepted"),
        P4KadanProjectConfig(Renderer, Project + TEXT("[/Script/Engine.RendererSettings]\nr.Own=3\n"), Rendered,
            Channels, Profiles, Why));
    TestTrue(TEXT("Game renderer settings copied"),
        Rendered.Contains(TEXT("r.ForwardShading=True")) && Rendered.Contains(TEXT("r.Custom=2")));
    TestFalse(TEXT("Creator value of a game renderer key replaced"), Rendered.Contains(TEXT("r.Custom=1")));
    TestTrue(TEXT("Creator renderer key the game does not set preserved"), Rendered.Contains(TEXT("r.Own=3")));
    TestEqual(TEXT("One renderer section"),
        Rendered.Len() - Rendered.Replace(TEXT("[/Script/Engine.RendererSettings]"), TEXT("")).Len(),
        FString(TEXT("[/Script/Engine.RendererSettings]")).Len());
    TestTrue(TEXT("Repeat renderer setup accepted"),
        P4KadanProjectConfig(Renderer, Rendered, RenderedAgain, Channels, Profiles, Why));
    TestEqual(TEXT("Repeat renderer setup is stable"), RenderedAgain, Rendered);
    Again = TEXT("Unchanged output");
    TestFalse(TEXT("Truncated channels refused"),
        P4KadanProjectConfig(
            Game.Replace(TEXT("+DefaultChannelResponses=(Channel=ECC_GameTraceChannel18,Name=\"Game18\")\n"), TEXT("")),
            Project, Again, Channels, Profiles, Why));
    TestEqual(TEXT("Refusal preserves caller output"), Again, FString(TEXT("Unchanged output")));
    TestFalse(TEXT("Repeated source collision section refused"),
        P4KadanProjectConfig(Game + Game, Project, Again, Channels, Profiles, Why));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanDeclarationTypes, "P4MapKit.Kadan.DeclarationTypes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKadanDeclarationTypes::RunTest(const FString&)
{
    for (const TCHAR* Module : {TEXT("P4StubsCore"), TEXT("Carrot4"), TEXT("AkAudio"), TEXT("EasyParticle"),
             TEXT("UIParticle"), TEXT("SwitchRuntimeSettings")})
        TestTrue(FString(Module) + TEXT(" is loaded"), FModuleManager::Get().IsModuleLoaded(Module));
    UClass* Settings = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.MyWorldSettings"));
    if (TestNotNull(TEXT("Original world settings path"), Settings))
        TestTrue(TEXT("World settings retains engine ancestry"), Settings->IsChildOf(AWorldSettings::StaticClass()));
    struct FCostRow
    {
        const TCHAR* Name;
        float Cost;
    };
    const FCostRow Costs[] = {{TEXT("NavArea_AvatarAndWorkPikmin"), 0.5f}, {TEXT("NavArea_ObstacleOff"), 10000.0f}};
    for (const FCostRow& Row : Costs)
    {
        const FString Name(Row.Name);
        UClass* Class = FindObject<UClass>(nullptr, *(TEXT("/Script/Carrot4.") + Name));
        if (!TestNotNull(Name + TEXT(" native class"), Class))
            return false;
        FFloatProperty* Cost = FindFProperty<FFloatProperty>(Class, TEXT("DefaultCost"));
        if (!TestNotNull(Name + TEXT(" native cost property"), Cost))
            return false;
        TestEqual(Name + TEXT(" constructor default without private config"),
            Cost->GetPropertyValue_InContainer(Class->GetDefaultObject()), Row.Cost);
    }
    struct FEnumRow
    {
        const TCHAR* Name;
        int32 Values;
    };
    const FEnumRow Enums[] = {
        {TEXT("EC4Primitive"), 3}, {TEXT("EC4Stencil"), 5}, {TEXT("EC4ExpontentialHeightFogBlend"), 3}};
    for (const FEnumRow& Row : Enums)
    {
        const FString Name(Row.Name);
        UEnum* Enum = FindObject<UEnum>(nullptr, *(TEXT("/Script/Engine.") + Name));
        if (TestNotNull(Name + TEXT(" original Engine identity"), Enum))
            TestEqual(Name + TEXT(" values including MAX"), Enum->NumEnums(), Row.Values);
    }
    UClass* Particle = FindObject<UClass>(nullptr, TEXT("/Script/Engine.ParticleLightManager"));
    if (TestNotNull(TEXT("Particle manager original identity"), Particle))
    {
        TestTrue(TEXT("Particle manager engine ancestry"), Particle->GetSuperClass() == UObject::StaticClass());
        TestEqual(TEXT("Particle manager has no extra native storage"), Particle->GetPropertiesSize(),
            UObject::StaticClass()->GetPropertiesSize());
        TestNotNull(TEXT("Particle manager native default construction"), Particle->GetDefaultObject());
    }
    UScriptStruct* Tag = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Engine.C4TerrainTag"));
    UScriptStruct* Container = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Engine.C4TerrainTagContainer"));
    if (!TestNotNull(TEXT("Terrain tag original identity"), Tag) ||
        !TestNotNull(TEXT("Terrain container original identity"), Container))
        return false;
    UClass* Collision = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.StaticCollisionComponent"));
    FStructProperty* CollisionTags = Collision
        ? FindFProperty<FStructProperty>(Collision, TEXT("C4TerrainTagContainer"))
        : nullptr;
    if (!TestNotNull(TEXT("The actual collision component declares its terrain field"), CollisionTags))
        return false;
    TestTrue(TEXT("Terrain remains an own collision-component field with the original Engine struct identity"),
        CollisionTags->GetOwnerClass() == Collision && CollisionTags->Struct == Container);
    TestEqual(TEXT("The component field retains the qualified terrain storage size"), CollisionTags->ElementSize, 16);
    TestTrue(TEXT("The actual native collision default safely constructs the terrain storage"),
        Collision && Collision->GetDefaultObject() &&
            CollisionTags->ContainerPtrToValuePtr<void>(Collision->GetDefaultObject()));
    FUInt32Property* Scalar = FindFProperty<FUInt32Property>(Tag, TEXT("TerrainTag"));
    FArrayProperty* Array = FindFProperty<FArrayProperty>(Container, TEXT("TerrainTags"));
    FStructProperty* Inner = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
    if (!TestNotNull(TEXT("Terrain tag native scalar"), Scalar) || !TestNotNull(TEXT("Terrain native array"), Array))
        return false;
    TestEqual(TEXT("Terrain scalar retains original offset"), Scalar->GetOffset_ForInternal(), 8);
    TestEqual(TEXT("Terrain array retains original offset"), Array->GetOffset_ForInternal(), 0);
    TestTrue(TEXT("Array inner retains the original tag identity"), Inner && Inner->Struct == Tag);
    for (UScriptStruct* Struct : {Tag, Container})
    {
        UScriptStruct::ICppStructOps* Ops = Struct->GetCppStructOps();
        if (!TestNotNull(Struct->GetName() + TEXT(" native operations"), Ops))
            return false;
        TestEqual(Struct->GetName() + TEXT(" native size"), Ops->GetSize(), 16);
        TestEqual(Struct->GetName() + TEXT(" reflected native size"), Struct->GetStructureSize(), 16);
        TestEqual(Struct->GetName() + TEXT(" native alignment"), Ops->GetAlignment(), 8);
        TestTrue(Struct->GetName() + TEXT(" native copy operation"), Ops->HasCopy());
    }
    if (HasAnyErrors())
        return false;
    // Exercise the owned C++ constructors, copies and destructors. A reflected
    // field-only stand-in cannot supply these operations or correct tag stride.
    void* Source = FMemory::Malloc(Container->GetStructureSize(), Container->GetMinAlignment());
    void* Copy = FMemory::Malloc(Container->GetStructureSize(), Container->GetMinAlignment());
    Container->InitializeStruct(Source);
    Container->InitializeStruct(Copy);
    FScriptArrayHelper SourceArray(Array, Array->ContainerPtrToValuePtr<void>(Source));
    SourceArray.AddValue();
    Scalar->SetPropertyValue_InContainer(SourceArray.GetRawPtr(0), 37u);
    Container->CopyScriptStruct(Copy, Source);
    FScriptArrayHelper CopyArray(Array, Array->ContainerPtrToValuePtr<void>(Copy));
    TestEqual(TEXT("Native array copy retains its element"), CopyArray.Num(), 1);
    if (CopyArray.Num() == 1)
    {
        TestEqual(TEXT("Native tag copy retains its value"),
            Scalar->GetPropertyValue_InContainer(CopyArray.GetRawPtr(0)), 37u);
        Scalar->SetPropertyValue_InContainer(SourceArray.GetRawPtr(0), 8u);
        TestEqual(TEXT("Native array copy owns its storage"),
            Scalar->GetPropertyValue_InContainer(CopyArray.GetRawPtr(0)), 37u);
    }
    Container->DestroyStruct(Copy);
    Container->DestroyStruct(Source);
    FMemory::Free(Copy);
    FMemory::Free(Source);
    return !HasAnyErrors();
}
// The setup's rules over the game's own configuration and declarations agree with the names they replaced: the
// eight navigation sections, the world settings class and the six declaration modules.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanProjectSetupRules, "P4MapKit.Kadan.ProjectSetupRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKadanProjectSetupRules::RunTest(const FString&)
{
    const FString NativeRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    const FString Repak = FPaths::GetPath(FPaths::GetPath(NativeRoot)) / TEXT("private/repak/repak.exe");
    const FString Pak = GamePaks / TEXT("Carrot4-Switch.pak");
    if (NativeRoot.IsEmpty() || !IFileManager::Get().FileExists(*Pak) || !IFileManager::Get().FileExists(*Repak))
    {
        AddError(TEXT("The game containers and repak are required."));
        return false;
    }
    TArray<uint8> Bytes;
    FString Why;
    if (!TestTrue(TEXT("The game configuration is read"),
            P4RunExportTool(Repak, {TEXT("get"), Pak, TEXT("Carrot4/Config/DefaultEngine.ini")}, 60.0, Bytes, Why)))
        return false;
    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
    FString GameIni(Converted.Length(), Converted.Get());
    FP4KadanGameConfigClasses Classes;
    if (!TestTrue(TEXT("The game configuration names its classes"), P4KadanGameConfigClasses(GameIni, Classes, Why)))
    {
        AddError(Why);
        return false;
    }

    const TArray<FString> OldSections = {TEXT("[/Script/NavigationSystem.RecastNavMesh]"),
        TEXT("[/Script/Carrot4.CarrotNavigationSystem]"), TEXT("[/Script/Carrot4.RecastNavMeshPikmin]"),
        TEXT("[/Script/Carrot4.RecastNavMeshPikminHero]"), TEXT("[/Script/Carrot4.RecastNavMeshOtakaraSmall]"),
        TEXT("[/Script/Carrot4.RecastNavMeshOtakaraSmallHero]"), TEXT("[/Script/Carrot4.RecastNavMeshOtakaraBig]"),
        TEXT("[/Script/Carrot4.RecastNavMeshOtakaraBigHero]")};
    TArray<FString> Old;
    TArray<FString> Lines;
    GameIni.ParseIntoArrayLines(Lines, false);
    for (const FString& Raw : Lines)
        if (OldSections.Contains(Raw.TrimStartAndEnd()))
            Old.Add(Raw.TrimStartAndEnd());
    AddInfo(FString::Printf(
        TEXT("Navigation sections: old rule %d, new rule %d"), Old.Num(), Classes.NavigationSections.Num()));
    TestEqual(TEXT("Both rules copy the same navigation sections"), Classes.NavigationSections, Old);
    TestEqual(TEXT("Both rules give the same world settings class"), Classes.WorldSettings,
        FString(TEXT("/Script/Carrot4.MyWorldSettings")));

    TArray<FString> OldModules = {TEXT("P4StubsCore"), TEXT("Carrot4"), TEXT("AkAudio"), TEXT("EasyParticle"),
        TEXT("UIParticle"), TEXT("SwitchRuntimeSettings")};
    TArray<FString> Modules = P4KadanDeclarationModules();
    OldModules.Sort();
    Modules.Sort();
    AddInfo(FString::Printf(TEXT("Declaration modules: old rule %d, new rule %d"), OldModules.Num(), Modules.Num()));
    TestEqual(TEXT("Both rules require the same declaration modules"), Modules, OldModules);

    // Every class of this editor: the old rule accepted one world settings class, the new one any declared one.
    int32 Seen = 0;
    TArray<FString> Declared;
    for (TObjectIterator<UClass> Class; Class; ++Class)
    {
        ++Seen;
        if (Class->IsChildOf(AWorldSettings::StaticClass()) && P4KadanDeclaredClass(*Class))
            Declared.Add(Class->GetPathName());
    }
    AddInfo(FString::Printf(
        TEXT("Declared world settings classes among %d classes: %s"), Seen, *FString::Join(Declared, TEXT(", "))));
    TestEqual(TEXT("Both rules accept the same world settings class"), Declared,
        TArray<FString>{TEXT("/Script/Carrot4.MyWorldSettings")});
    return !HasAnyErrors();
}
#endif
