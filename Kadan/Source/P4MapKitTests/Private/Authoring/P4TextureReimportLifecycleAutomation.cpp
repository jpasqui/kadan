#if P4_MAPKIT_TESTS
#include "P4BlenderExport.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

// Opt-in diagnostic. Reimport one unchanged retained texture while its actual material users are
// loaded, rather than reimporting an entire map. The optional wait belongs only to this experiment;
// it does not change production imports, texture settings, source comparisons or sampler policy.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureReimportLifecycle, "P4MapKit.Kadan.TextureReimportLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureReimportLifecycle::RunTest(const FString&)
{
    const FString Package = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_TEXTURE_REIMPORT_PACKAGE"));
    const FString MaterialFolder = FPlatformMisc::GetEnvironmentVariable(
        TEXT("P4_NATIVE_TEXTURE_REIMPORT_MATERIAL_FOLDER"));
    const FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_TEXTURE_REIMPORT_GAME"));
    const FString Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_TEXTURE_REIMPORT_RETOC"));
    const FString Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_TEXTURE_REIMPORT_REPAK"));
    if (Package.IsEmpty() || MaterialFolder.IsEmpty() || Game.IsEmpty() || Retoc.IsEmpty() || Repak.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Set the five P4_NATIVE_TEXTURE_REIMPORT inputs."));
        return true;
    }
    if (GUsingNullRHI)
    {
        AddWarning(TEXT("P4MapKit test not run. Texture lifecycle diagnostic needs a real RHI."));
        return true;
    }
    if (!TestTrue(TEXT("Texture input is a canonical game package"),
            Package.StartsWith(TEXT("/Game/")) && FPackageName::IsValidLongPackageName(Package)))
        return false;
    const FString ObjectPath = Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
    UTexture* Initial = LoadObject<UTexture>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!TestNotNull(TEXT("An existing retained texture is loaded"), Initial))
        return false;

    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FAssetData> Candidates;
    Registry.GetAssetsByPath(FName(*MaterialFolder), Candidates, true);
    TArray<TStrongObjectPtr<UMaterialInterface>> Materials;
    int32 ActualUsers = 0;
    for (const FAssetData& Data : Candidates)
    {
        if (Data.AssetClass != TEXT("Material") && Data.AssetClass != TEXT("MaterialInstanceConstant"))
            continue;
        UMaterialInterface* Material = Cast<UMaterialInterface>(Data.GetAsset());
        if (!Material)
            continue;
        Materials.Emplace(Material);
        TArray<UTexture*> Used;
        Material->GetUsedTextures(Used, EMaterialQualityLevel::High, true, GMaxRHIFeatureLevel, true);
        if (Used.Contains(Initial))
            ++ActualUsers;
        Material->GetRenderProxy();
    }
    if (!TestTrue(TEXT("At least one loaded actual material user references this texture"), ActualUsers > 0))
        return false;
    AddInfo(FString::Printf(TEXT("Texture lifecycle loaded %d material interfaces, %d actual texture users."),
        Materials.Num(), ActualUsers));
    Initial->WaitForPendingInitOrStreaming();
    FlushRenderingCommands(); // Settle initial loads before the reimport experiment begins.
    Initial = nullptr; // Do not keep the old texture alive across native package reload/GC.

    const bool Wait = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_TEXTURE_REIMPORT_WAIT")) == TEXT("1");
    int32 Fixups = 0, Waits = 0, Iteration = 0;
    const FDelegateHandle Handle = FCoreUObjectDelegates::OnPackageReloaded.AddLambda(
        [&](EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
        {
            if (!Event)
                return;
            const UPackage* Old = Event->GetOldPackage();
            const UPackage* New = Event->GetNewPackage();
            if ((!Old || Old->GetName() != Package) && (!New || New->GetName() != Package))
                return;
            AddInfo(FString::Printf(TEXT("Texture lifecycle iteration %d phase %d old=%s new=%s"), Iteration,
                int32(Phase), Old ? *Old->GetName() : TEXT("none"), New ? *New->GetName() : TEXT("none")));
            if (Phase != EPackageReloadPhase::PrePackageFixup || !New || New->GetName() != Package)
                return;
            ++Fixups;
            UTexture* Replacement = FindObject<UTexture>(nullptr, *ObjectPath);
            if (!Replacement || Replacement->GetOutermost() != New)
            {
                AddError(TEXT("Owned replacement texture is absent before fixup."));
                return;
            }
            if (Wait)
            {
                Replacement->WaitForPendingInitOrStreaming();
                ++Waits;
                AddInfo(FString::Printf(
                    TEXT("Texture lifecycle iteration %d replacement initialization wait ended before fixup."),
                    Iteration));
            }
        });
    ON_SCOPE_EXIT
    {
        FCoreUObjectDelegates::OnPackageReloaded.Remove(Handle);
    };
    for (Iteration = 1; Iteration <= 3; ++Iteration)
    {
        const FString Text = UKadanBlender::ImportGamePackages(Game, Retoc, {Package}, true, Repak, true);
        TSharedPtr<FJsonObject> Summary, Report;
        const TSharedPtr<FJsonObject>* ImportedPackages = nullptr;
        const TSharedPtr<FJsonObject>* Assets = nullptr;
        const TSharedPtr<FJsonObject>* Reply = nullptr;
        FString NativeText;
        bool Ok = false, Valid = false, Saved = false;
        const bool Parsed = FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Summary) &&
            Summary.IsValid();
        // The public script API returns a summary. Its successful native call writes the full current
        // receipt; require both, so a preserved texture or a precondition failure cannot pass.
        const bool Imported = Parsed && Summary->TryGetBoolField(TEXT("ok"), Ok) && Ok &&
            Summary->TryGetObjectField(TEXT("imported"), ImportedPackages) && (*ImportedPackages)->HasField(Package) &&
            FFileHelper::LoadFileToString(
                NativeText, *(FPaths::ProjectSavedDir() / TEXT("Kadan/import-result.json"))) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(NativeText), Report) && Report.IsValid() &&
            Report->TryGetObjectField(TEXT("assets"), Assets) && (*Assets)->TryGetObjectField(Package, Reply) &&
            (*Reply)->TryGetBoolField(TEXT("valid"), Valid) && Valid &&
            (*Reply)->TryGetBoolField(TEXT("saved"), Saved) && Saved;
        if (!TestTrue(
                FString::Printf(TEXT("Iteration %d actually saves and cold-verifies the retained texture"), Iteration),
                Imported))
        {
            AddInfo(TEXT("Texture lifecycle failed import receipt: ") + Text.Left(12000));
            return false;
        }
    }
    TestEqual(TEXT("Every iteration reaches the owned replacement fixup"), Fixups, 3);
    TestEqual(TEXT("Initialization wait occurs only in the opt-in candidate"), Waits, Wait ? 3 : 0);
    return !HasAnyErrors();
}
#endif
