#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialGraph.h"
#include "P4MaterialGameSettings.h"
#include "P4SwitchShaderMapFreeze.h"
#include "AssetRegistryModule.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "Shader.h"
#include "ShaderCompiler.h"
#include "UObject/StrongObjectPtr.h"
#include "VertexFactory.h"

namespace
{
FString Graph(bool bSSR, bool bSceneColorOrder)
{
    return FString::Printf(TEXT(R"JSON({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Translucent",
        "shading_model":"MSM_DefaultLit","two_sided":false,"tangent_space_normal":true,
        "render_settings":{"bScreenSpaceReflections":%s,"C4MaterialDrawOrder":"%s",
        "TranslucencyLightingMode":"TLM_SurfacePerPixelLighting","bEnableSeparateTranslucency":false,
        "bUseTranslucencyVertexFog":false,"bUsedWithStaticLighting":true},
        "nodes":{"Value":{"type":"Constant","inputs":{},"props":{"R":0.5}},
        "Normal":{"type":"Constant3Vector","inputs":{},"props":{"Constant":[0.0,0.0,1.0,0.0]}}},
        "outputs":{"BaseColor":["Value",""],"EmissiveColor":["Value",""],"Opacity":["Value",""],
        "Normal":["Normal",""]}})JSON"),
        bSSR ? TEXT("true") : TEXT("false"), bSceneColorOrder ? TEXT("C4MDO_XluUseSceneColor") : TEXT("C4MDO_Default"));
}

void Release(UMaterial* Material)
{
    if (!Material)
        return;
    FAssetRegistryModule::AssetDeleted(Material);
    Material->ClearFlags(RF_Public | RF_Standalone);
    if (Material->Rename(
            nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders))
        Material->MarkPendingKill();
}

struct FMembership
{
    TArray<FString> Base;
    TArray<FString> Xlu;
    TArray<FString> ExpectedXlu;
};

FMembership Membership(const FMaterialResource& Resource, bool bAfterGamePrune = false)
{
    FMembership Out;
    TMap<FShaderId, TShaderRef<FShader>> Shaders;
    Resource.GetGameThreadShaderMap()->GetShaderList(Shaders);
    for (const auto& Pair : Shaders)
    {
        if (!Pair.Key.Type || !Pair.Key.VFType)
            continue;
        const FString Type = Pair.Key.Type->GetName();
        if (!Type.StartsWith(TEXT("TBasePassPS"), ESearchCase::CaseSensitive))
            continue;
        const FString Prefix = FString(Pair.Key.VFType->GetName()) + TEXT("|");
        const FString Suffix = TEXT("|") + FString::FromInt(Pair.Key.PermutationId);
        const bool bXlu = Type.EndsWith(TEXT("XluPassSSR"), ESearchCase::CaseSensitive) ||
            Type.EndsWith(TEXT("XluPassRecombine"), ESearchCase::CaseSensitive);
        if (bXlu)
            Out.Xlu.Add(Prefix + Type + Suffix);
        else
        {
            Out.Base.Add(Prefix + Type + Suffix);
            // Existing game serialization removes the two cached-lighting policies.
            if (bAfterGamePrune && Type.StartsWith(TEXT("TBasePassPSFCached"), ESearchCase::CaseSensitive))
                continue;
            Out.ExpectedXlu.Add(Prefix + Type + TEXT("XluPassSSR") + Suffix);
            Out.ExpectedXlu.Add(Prefix + Type + TEXT("XluPassRecombine") + Suffix);
        }
    }
    Out.Base.Sort();
    Out.Xlu.Sort();
    Out.ExpectedXlu.Sort();
    return Out;
}

struct FReleaseResource
{
    void operator()(FMaterialResource* Resource) const { P4ReleaseMaterialResource(Resource); }
};
using FHeldResource = TUniquePtr<FMaterialResource, FReleaseResource>;

bool Cache(FAutomationTestBase& Test, UMaterial* Material, FHeldResource& Out)
{
    Out.Reset(P4CacheMaterialForPlatform(Material, SP_SWITCH));
    if (!Test.TestNotNull(TEXT("The owned Switch resource was allocated"), Out.Get()))
        return false;
    if (GShaderCompilingManager)
        GShaderCompilingManager->FinishAllCompilation();
    Out->FinishCompilation();
    FMaterialShaderMap* Map = Out->GetGameThreadShaderMap();
    if (!Test.TestNotNull(TEXT("The map exists before freezing"), Map))
        return false;
    return Test.TestTrue(TEXT("The map compiled successfully before freezing"),
               Map->IsCompilationFinalized() && Map->CompiledSuccessfully()) &&
        Test.TestTrue(TEXT("The resource has finished compiling"), Out->IsCompilationFinished()) &&
        Test.TestTrue(TEXT("The resource has a valid game-thread map"), Out->HasValidGameThreadShaderMap());
}

bool Ordered(FAutomationTestBase& Test, bool bTrueFirst)
{
    if (!Test.TestTrue(TEXT("Run this selector in a fresh editor with KadanShaderFormat already loaded"),
            FModuleManager::Get().IsModuleLoaded(TEXT("KadanShaderFormat"))))
        return false;
    FP4SwitchShadersInEditorProcess InEditor;
    const FString Root = FString(TEXT("/Game/P4MapKitSSRIsolation/")) +
        (bTrueFirst ? TEXT("TrueThenFalse/") : TEXT("FalseThenTrue/"));
    for (const TCHAR* Name : {TEXT("M_Off"), TEXT("M_On"), TEXT("M_NoSceneColor")})
    {
        const FString Package = Root + Name;
        if (!Test.TestTrue(TEXT("Only a fresh unsaved test namespace is used"),
                !FPackageName::DoesPackageExist(Package) &&
                    !FindObject<UObject>(nullptr, *(Package + TEXT(".") + Name))))
            return false;
    }
    FString Error;
    TStrongObjectPtr<UMaterial> Off(P4BuildMaterialFromGraph(Graph(false, true), Root + TEXT("M_Off"), Error));
    if (!Off.IsValid())
    {
        Test.AddError(Error);
        return false;
    }
    TStrongObjectPtr<UMaterial> On(P4BuildMaterialFromGraph(Graph(true, true), Root + TEXT("M_On"), Error));
    TStrongObjectPtr<UMaterial> NoSceneColor(
        P4BuildMaterialFromGraph(Graph(true, false), Root + TEXT("M_NoSceneColor"), Error));
    FHeldResource OffResource, OnResource, NoSceneColorResource, AgainOff, AgainOn;
    ON_SCOPE_EXIT
    {
        AgainOff.Reset();
        AgainOn.Reset();
        OffResource.Reset();
        OnResource.Reset();
        NoSceneColorResource.Reset();
        Release(Off.Get());
        Release(On.Get());
        Release(NoSceneColor.Get());
    };
    if (!On.IsValid() || !NoSceneColor.IsValid())
    {
        Test.AddError(Error);
        return false;
    }
    if (!Test.TestFalse(TEXT("The imported explicit false is live"), Off->bScreenSpaceReflections) ||
        !Test.TestTrue(TEXT("The imported true is live"), On->bScreenSpaceReflections))
        return false;
    if (bTrueFirst)
    {
        if (!Cache(Test, On.Get(), OnResource) || !Cache(Test, Off.Get(), OffResource))
            return false;
    }
    else if (!Cache(Test, Off.Get(), OffResource) || !Cache(Test, On.Get(), OnResource))
        return false;

    const FMaterialShaderParameters OffParameters(OffResource.Get()), OnParameters(OnResource.Get());
    if (!Test.TestTrue(TEXT("The pair has identical native layout-key bytes despite different SSR"),
            FMemory::Memcmp(&OffParameters, &OnParameters, sizeof(OffParameters)) == 0))
        return false;
    FSHAHash ParameterHash;
    FSHA1::HashBuffer(&OffParameters, sizeof(OffParameters), ParameterHash.Hash);
    Test.AddInfo(FString::Printf(TEXT("SSR isolation order=%s parameters=%s"),
        bTrueFirst ? TEXT("true-false") : TEXT("false-true"), *ParameterHash.ToString()));
    const FMembership OffBefore = Membership(*OffResource), OnBefore = Membership(*OnResource);
    if (!Test.TestTrue(TEXT("Both maps have the same non-Xlu base pixel permutations"),
            OffBefore.Base.Num() > 0 && OffBefore.Base == OnBefore.Base) ||
        !Test.TestEqual(TEXT("False SSR has no Xlu shaders before freezing"), OffBefore.Xlu.Num(), 0) ||
        !Test.TestTrue(TEXT("True SSR has exactly both Xlu variants for every eligible base permutation"),
            OnBefore.ExpectedXlu.Num() > 0 && OnBefore.Xlu == OnBefore.ExpectedXlu))
        return false;

    // This is same-process map reuse, not evidence of a disk-DDC hit.
    const FGuid OffState = Off->StateId, OnState = On->StateId;
    if (!Cache(Test, Off.Get(), AgainOff) || !Cache(Test, On.Get(), AgainOn))
        return false;
    if (!Test.TestTrue(
            TEXT("Recaching preserves material identities"), Off->StateId == OffState && On->StateId == OnState) ||
        !Test.TestTrue(TEXT("False recache keeps its empty Xlu set"), Membership(*AgainOff).Xlu == OffBefore.Xlu) ||
        !Test.TestTrue(TEXT("True recache keeps its complete Xlu set"), Membership(*AgainOn).Xlu == OnBefore.Xlu))
        return false;
    AgainOff.Reset();
    AgainOn.Reset();
    if (!Cache(Test, NoSceneColor.Get(), NoSceneColorResource) ||
        !Test.TestEqual(TEXT("SSR true without scene-color draw order has no Xlu shaders"),
            Membership(*NoSceneColorResource).Xlu.Num(), 0))
        return false;

    // Freeze is exercised only after all pre-freeze membership/cache assertions.
    // Existing serialization prunes unrelated stock classes in-place.
    TArray<uint8> Zero, OffBytes, OnBytes;
    Zero.SetNumZeroed(20);
    TArray<FName> OffNames, OnNames;
    if (!P4FreezeSwitchShaderMap(*OffResource, Zero, Zero, OffNames, OffBytes, Error) ||
        !P4FreezeSwitchShaderMap(*OnResource, Zero, Zero, OnNames, OnBytes, Error))
    {
        Test.AddError(Error);
        return false;
    }
    const FMembership OnAfter = Membership(*OnResource, true);
    return Test.TestTrue(TEXT("Both game resources were serialized"), OffBytes.Num() > 0 && OnBytes.Num() > 0) &&
        Test.TestEqual(TEXT("False SSR remains empty after game pruning"), Membership(*OffResource).Xlu.Num(), 0) &&
        Test.TestTrue(TEXT("True SSR retains exactly the game-eligible Xlu pairs after pruning"),
            OnAfter.Xlu.Num() > 0 && OnAfter.Xlu == OnAfter.ExpectedXlu);
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SwitchSSRFalseFirstTest, "P4MapKit.Kadan.SwitchSSRIsolation.FalseThenTrue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SwitchSSRFalseFirstTest::RunTest(const FString&)
{
    return Ordered(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SwitchSSRTrueFirstTest, "P4MapKit.Kadan.SwitchSSRIsolation.TrueThenFalse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SwitchSSRTrueFirstTest::RunTest(const FString&)
{
    return Ordered(*this, true);
}
#endif
