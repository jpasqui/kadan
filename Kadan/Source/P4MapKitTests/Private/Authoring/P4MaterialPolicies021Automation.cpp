#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "Dom/JsonObject.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialPreviewPolicies021, "P4MapKit.Kadan.MaterialPreviewPolicies021",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialPreviewPolicies021::RunTest(const FString&)
{
    const FString Folder = TEXT("/Game/KadanMaterialPolicy021Tests/");
    TArray<UObject*> Made;
    ON_SCOPE_EXIT
    {
        for (int32 I = Made.Num() - 1; I >= 0; --I)
            if (Made[I])
            {
                Made[I]->ClearFlags(RF_Public | RF_Standalone);
                Made[I]->Rename(nullptr, GetTransientPackage(),
                    REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
                Made[I]->MarkPendingKill();
            }
    };
    FString Error;
    const FString Decal = TEXT(
        R"JSON({"domain":"MD_DeferredDecal","blend_mode":"BLEND_Translucent","shading_model":"MSM_DefaultLit","two_sided":false,"clip":0.3333,"tangent_space_normal":true,"render_settings":{"DecalBlendMode":"DBM_DBuffer_C4_WaterBox"},"nodes":{"r":{"type":"Constant","inputs":{},"props":{"R":0.2}}},"outputs":{"BaseColor":["r",""]}})JSON");
    UMaterial* Water = P4BuildMaterialFromGraph(Decal, Folder + TEXT("M_WaterBox"), Error);
    if (!TestNotNull(TEXT("WaterBox builds with a stock preview policy"), Water))
    {
        AddError(Error);
        return false;
    }
    Made.Add(Water);
    TestEqual(TEXT("stock color DBuffer stand-in"), int32(Water->DecalBlendMode), int32(DBM_DBuffer_Color));
    const FString ReadWater = P4MaterialGraphJson(Water, Error);
    TSharedPtr<FJsonObject> Read;
    if (!TestTrue(TEXT("WaterBox graph reads back"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ReadWater), Read) && Read.IsValid()))
        return false;
    TestEqual(TEXT("game enum is retained for cooking"),
        Read->GetObjectField(TEXT("render_settings"))->GetStringField(TEXT("DecalBlendMode")),
        FString(TEXT("DBM_DBuffer_C4_WaterBox")));
    const FString ParentGraph = TEXT(
        R"JSON({"domain":"MD_Surface","blend_mode":"BLEND_Opaque","shading_model":"MSM_DefaultLit","two_sided":false,"clip":0.3333,"tangent_space_normal":true,"nodes":{"t":{"type":"TextureSampleParameter2D","inputs":{},"props":{"ParameterName":"Diffuse","Texture":"/Engine/EngineResources/DefaultTexture.DefaultTexture","SamplerType":"SAMPLERTYPE_Color"}}},"outputs":{"BaseColor":["t","RGB"]}})JSON");
    UMaterial* Parent = P4BuildMaterialFromGraph(ParentGraph, Folder + TEXT("M_Parent"), Error);
    if (!TestNotNull(TEXT("null texture test parent builds"), Parent))
    {
        AddError(Error);
        return false;
    }
    Made.Add(Parent);
    const FString InstanceJson = FString(TEXT("{\"parent\":\"")) + Parent->GetPathName() +
        TEXT("\",\"textures\":{\"Diffuse\":null},\"scalars\":{},\"vectors\":{},\"switches\":{},\"overrides\":{}}");
    TArray<FString> Missing;
    UMaterialInstanceConstant* Instance = P4BuildMaterialInstance(
        InstanceJson, Folder + TEXT("MI_Null"), Missing, Error);
    if (!TestNotNull(TEXT("explicit null texture constructs"), Instance))
    {
        AddError(Error);
        return false;
    }
    Made.Add(Instance);
    UTexture* Value = reinterpret_cast<UTexture*>(UPTRINT(1));
    TestTrue(TEXT("null remains a local override, distinct from parent inheritance"),
        Instance->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Diffuse")), Value) && Value == nullptr);
    TestEqual(TEXT("null is retained in the parameter array"), Instance->TextureParameterValues.Num(), 1);
    TSharedPtr<FJsonObject> ReadInstance;
    const FString Text = P4MaterialInstanceJson(Instance, Error);
    if (!TestTrue(TEXT("null instance reads back"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), ReadInstance) && ReadInstance.IsValid()))
        return false;
    TestTrue(TEXT("null round-trips as null, without an asset path"),
        ReadInstance->GetObjectField(TEXT("textures"))->HasTypedField<EJson::Null>(TEXT("Diffuse")));
    return true;
}
#endif
