// Private native regression draft, bound to the full 138/27 measured slice.
// Not compiled/executed by this worker. No feature or qualification claim.
#include "P4AuthoringRows.h"
#include "P4NativeValueStaging.h"
#include "P4NativeHash.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Curves/CurveVector.h"
#include "Animation/AnimClassData.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/SoftObjectPtr.h"
#include "Async/Async.h"

#include "P4AuthoringRowsCoverageShared.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows138SceneClassSoftPathAndArrayMatrix,
    "P4MapKit.Quality.Rows138SceneClassSoftPathAndArrayMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows138SceneClassSoftPathAndArrayMatrix::RunTest(const FString&)
{
    using namespace P4RowsCoverage138;
    FN138Suite F(*this, TEXT("SceneClassSoftPathAndArrayMatrix"));
    auto Class = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.GameSceneData"));
    auto Procedure = Class ? CastField<FClassProperty>(Class->FindPropertyByName(TEXT("SceneProc"))) : nullptr;
    auto World = Class ? CastField<FSoftObjectProperty>(Class->FindPropertyByName(TEXT("OpenLevel"))) : nullptr;
    auto Parent = Class ? CastField<FObjectProperty>(Class->FindPropertyByName(TEXT("ParentScene"))) : nullptr;
    auto Assets = Class ? CastField<FArrayProperty>(Class->FindPropertyByName(TEXT("LoadAssets"))) : nullptr;
    // Actual retained UE create/reload descriptors: LoadAssets→soft UObject,
    // LoadBlueprints→soft UClass and LoadLevels→soft UWorld. These are never
    // hard-dependency arrays; absent soft targets must remain representable.
    auto AssetInner = Assets ? CastField<FSoftObjectProperty>(Assets->Inner) : nullptr;
    auto Blueprints = Class ? CastField<FArrayProperty>(Class->FindPropertyByName(TEXT("LoadBlueprints"))) : nullptr;
    auto BlueprintInner = Blueprints ? CastField<FSoftClassProperty>(Blueprints->Inner) : nullptr;
    auto Levels = Class ? CastField<FArrayProperty>(Class->FindPropertyByName(TEXT("LoadLevels"))) : nullptr;
    auto LevelInner = Levels ? CastField<FSoftObjectProperty>(Levels->Inner) : nullptr;
    auto DefaultProcedure = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.SPDefault"));
    if (!F.Check(TEXT("Actual recovered scene class/reference schema prerequisites"),
            Class && Procedure && Procedure->MetaClass && World && World->PropertyClass && Parent &&
                Parent->PropertyClass == Class && Assets && AssetInner && AssetInner->PropertyClass &&
                AssetInner->PropertyClass == UObject::StaticClass() && BlueprintInner &&
                BlueprintInner->PropertyClass == UClass::StaticClass() && LevelInner &&
                LevelInner->PropertyClass == World->PropertyClass && DefaultProcedure &&
                DefaultProcedure->IsChildOf(Procedure->MetaClass)))
        return false;
    TStrongObjectPtr<UObject> Scene(NewObject<UObject>(
        CreatePackage(*(F.PackageRoot / TEXT("Scene"))), Class, TEXT("Scene"), RF_Transient | RF_Transactional));
    TStrongObjectPtr<UObject> Sibling(NewObject<UObject>(
        CreatePackage(*(F.PackageRoot / TEXT("Sibling"))), Class, TEXT("Sibling"), RF_Transient | RF_Transactional));
    TStrongObjectPtr<UDataTable> Wrong(
        NewObject<UDataTable>(CreatePackage(*(F.PackageRoot / TEXT("NonClass"))), TEXT("NonClass"), RF_Transient));
    Wrong->RowStruct = FTableRowBase::StaticStruct(); // Keep the wrong-class fixture valid through later native GC.
    if (!F.Check(TEXT("Concrete native array fixture is admitted by actual reflected object base"),
            UCurveVector::StaticClass()->IsChildOf(AssetInner->PropertyClass)))
        return false;
    TStrongObjectPtr<UCurveVector> ArrayTarget(NewObject<UCurveVector>(
        CreatePackage(*(F.PackageRoot / TEXT("ArrayTarget"))), TEXT("ArrayTarget"), RF_Transient));
    const FString Initial = TEXT("{\"SceneProc\":") + N138Quote(DefaultProcedure->GetPathName()) +
        TEXT(",\"ParentScene\":") + N138Quote(Sibling->GetPathName()) + TEXT(",\"OpenLevel\":\"\",\"LoadAssets\":[]}");
    if (!F.Check(TEXT("Native genuine class/hard/soft/array positive admission"),
            UP4AuthoringTools::NativeApplyAssetValues(Scene.Get(), Initial)))
        return false;
    F.Verdict(TEXT("Native initial scene readback"), Initial,
        UP4AuthoringTools::NativeAssetReadback(Scene.Get(), Initial), true);
    const FString Unresolved = F.PackageRoot / TEXT("UnresolvedWorld.UnresolvedWorld");
    const FString UnresolvedAsset = F.PackageRoot / TEXT("UnresolvedAsset.UnresolvedAsset"),
                  UnresolvedClass = F.PackageRoot / TEXT("BP_Unresolved.BP_Unresolved_C");
    const FString Positive = TEXT("{\"SceneProc\":") + N138Quote(DefaultProcedure->GetPathName()) +
        TEXT(",\"ParentScene\":") + N138Quote(Sibling->GetPathName()) + TEXT(",\"OpenLevel\":") +
        N138Quote(Unresolved) + TEXT(",\"LoadAssets\":[") + N138Quote(ArrayTarget->GetPathName()) + TEXT(",") +
        N138Quote(UnresolvedAsset) + TEXT(",\"\"],\"LoadBlueprints\":[") + N138Quote(DefaultProcedure->GetPathName()) +
        TEXT(",") + N138Quote(UnresolvedClass) + TEXT(",\"\"],\"LoadLevels\":[") + N138Quote(Unresolved) +
        TEXT(",\"\"]}");
    if (!F.Check(TEXT("Loaded/unresolved/empty actual soft-array paths apply"),
            UP4AuthoringTools::NativeApplyAssetValues(Scene.Get(), Positive)))
        return false;
    F.Check(TEXT("Exact real native MetaClass pointer"),
        Procedure->GetObjectPropertyValue_InContainer(Scene.Get()) == DefaultProcedure);
    F.Check(TEXT("Exact hard parent pointer retained"),
        Parent->GetObjectPropertyValue_InContainer(Scene.Get()) == Sibling.Get());
    const auto Soft = World->GetPropertyValue_InContainer(Scene.Get());
    F.Check(TEXT("Unresolved soft world path retains exact native string without loading"),
        Soft.ToSoftObjectPath().ToString() == Unresolved && !Soft.Get());
    FScriptArrayHelper Array(Assets, Assets->ContainerPtrToValuePtr<void>(Scene.Get()));
    if (!F.Check(TEXT("Three actual native soft asset slots exist"), Array.Num() == 3))
        return false;
    F.Check(TEXT("Loaded soft asset retains exact path and resolved pointer"),
        AssetInner->GetPropertyValue(Array.GetRawPtr(0)).ToSoftObjectPath().ToString() == ArrayTarget->GetPathName() &&
            AssetInner->GetPropertyValue(Array.GetRawPtr(0)).Get() == ArrayTarget.Get());
    F.Check(TEXT("Unresolved soft asset retains exact path without forced load"),
        AssetInner->GetPropertyValue(Array.GetRawPtr(1)).ToSoftObjectPath().ToString() == UnresolvedAsset &&
            !AssetInner->GetPropertyValue(Array.GetRawPtr(1)).Get());
    F.Check(TEXT("Empty soft asset slot stays null"),
        AssetInner->GetPropertyValue(Array.GetRawPtr(2)).ToSoftObjectPath().IsNull());
    FScriptArrayHelper BlueprintArray(Blueprints, Blueprints->ContainerPtrToValuePtr<void>(Scene.Get())),
        LevelArray(Levels, Levels->ContainerPtrToValuePtr<void>(Scene.Get()));
    if (!F.Check(
            TEXT("Actual native soft class/world array counts"), BlueprintArray.Num() == 3 && LevelArray.Num() == 2))
        return false;
    F.Check(TEXT("Loaded soft class retains exact class path/pointer"),
        BlueprintInner->GetPropertyValue(BlueprintArray.GetRawPtr(0)).ToSoftObjectPath().ToString() ==
                DefaultProcedure->GetPathName() &&
            BlueprintInner->GetPropertyValue(BlueprintArray.GetRawPtr(0)).Get() == DefaultProcedure);
    F.Check(TEXT("Unresolved soft class retains exact native path"),
        BlueprintInner->GetPropertyValue(BlueprintArray.GetRawPtr(1)).ToSoftObjectPath().ToString() ==
                UnresolvedClass &&
            !BlueprintInner->GetPropertyValue(BlueprintArray.GetRawPtr(1)).Get());
    F.Check(TEXT("Empty soft class slot stays null"),
        BlueprintInner->GetPropertyValue(BlueprintArray.GetRawPtr(2)).ToSoftObjectPath().IsNull());
    F.Check(TEXT("Unresolved soft world array path stays exact"),
        LevelInner->GetPropertyValue(LevelArray.GetRawPtr(0)).ToSoftObjectPath().ToString() == Unresolved &&
            !LevelInner->GetPropertyValue(LevelArray.GetRawPtr(0)).Get());
    F.Check(TEXT("Empty soft world array slot stays null"),
        LevelInner->GetPropertyValue(LevelArray.GetRawPtr(1)).ToSoftObjectPath().IsNull());
    F.Verdict(TEXT("Exact unresolved soft path native readback"), Positive,
        UP4AuthoringTools::NativeAssetReadback(Scene.Get(), Positive), true);
    Scene->GetOutermost()->GetMetaData()->SetValue(Scene.Get(), TEXT("P4Rows138Sentinel"), TEXT("retain-scene"));
    Scene->GetOutermost()->SetDirtyFlag(false);
    FN138State Held(Scene.Get()), HeldSibling(Sibling.Get()), HeldArrayTarget(ArrayTarget.Get());
    Held.Same(F);
    HeldSibling.Same(F);
    HeldArrayTarget.Same(F);
    if (!F.Good)
        return false;
    const TArray<FString> Bad = {TEXT("{\"SceneProc\":") + N138Quote(Wrong->GetPathName()) + TEXT("}"),
        TEXT("{\"SceneProc\":\"/Script/Engine.Texture2D\"}"), TEXT("{\"SceneProc\":17}"),
        TEXT("{\"SceneProc\":\"/Game/NativeQuality/Absent.Absent\"}"),
        TEXT("{\"ParentScene\":") + N138Quote(Wrong->GetPathName()) + TEXT("}"),
        TEXT("{\"ParentScene\":\"/Script/Carrot4.SPDefault\"}"), TEXT("{\"OpenLevel\":null}"),
        TEXT("{\"OpenLevel\":true}"), TEXT("{\"OpenLevel\":{}}"),
        TEXT("{\"OpenLevel\":\"/Game/World\\u0000tail.World\"}"),
        TEXT("{\"OpenLevel\":") + N138Quote(FString::ChrN(NAME_SIZE, TEXT('P'))) + TEXT("}"),
        TEXT("{\"LoadAssets\":{}}"),
        TEXT("{\"LoadAssets\":[") + N138Quote(ArrayTarget->GetPathName()) + TEXT(",false]}"),
        TEXT("{\"LoadAssets\":[") + N138Quote(ArrayTarget->GetPathName()) + TEXT(",null]}"),
        TEXT("{\"LoadAssets\":[") + N138Quote(ArrayTarget->GetPathName()) + TEXT(",\"/Game/Asset\\u0000tail.Asset\"]}"),
        TEXT("{\"LoadBlueprints\":[\"/Script/Carrot4.SPDefault\",null]}"),
        TEXT("{\"LoadBlueprints\":[\"/Script/Carrot4.SPDefault\",false]}"),
        TEXT("{\"LoadLevels\":[") + N138Quote(Unresolved) + TEXT(",{}]}"),
        TEXT("{\"LoadLevels\":[") + N138Quote(Unresolved) + TEXT(",17]}"),
        TEXT("{\"OpenLevel\":\"\",\"SceneProc\":") + N138Quote(Wrong->GetPathName()) + TEXT("}")};
    for (const auto& Input : Bad)
    {
        F.Verdict(TEXT("Actual native scene field/reference refusal"), Input,
            UP4AuthoringTools::NativeCanonicalAssetValues(Scene.Get(), Input), false);
        const bool Applied = UP4AuthoringTools::NativeApplyAssetValues(Scene.Get(), Input);
        F.Trace(TEXT("Actual atomic native scene apply refusal"), Input, Applied ? TEXT("true") : TEXT("false"));
        F.Check(TEXT("No late invalid reference/path partially replaces native fields"), !Applied);
        Held.Same(F);
        HeldSibling.Same(F);
        HeldArrayTarget.Same(F);
    }
    // Stable null spellings use the genuine hard-property behavior; soft empty
    // paths remain strings. Compare each real pointer/path independently.
    for (const FString& Null : {FString(TEXT("null")), FString(TEXT("\"None\"")), FString(TEXT("\"\""))})
    {
        const FString Input = TEXT("{\"SceneProc\":") + Null + TEXT(",\"ParentScene\":") + Null +
            TEXT(",\"OpenLevel\":\"\",\"LoadAssets\":[],\"LoadBlueprints\":[],\"LoadLevels\":[]}");
        if (!F.Check(TEXT("Actual native explicit null control"),
                UP4AuthoringTools::NativeApplyAssetValues(Scene.Get(), Input)))
            return false;
        F.Check(TEXT("Actual native hard references both null"),
            !Procedure->GetObjectPropertyValue_InContainer(Scene.Get()) &&
                !Parent->GetObjectPropertyValue_InContainer(Scene.Get()));
        F.Check(TEXT("Actual native soft reference empty"),
            World->GetPropertyValue_InContainer(Scene.Get()).ToSoftObjectPath().IsNull());
        FScriptArrayHelper Empty(Assets, Assets->ContainerPtrToValuePtr<void>(Scene.Get()));
        F.Check(TEXT("Actual native array empty"), Empty.Num() == 0);
        FScriptArrayHelper EmptyClasses(Blueprints, Blueprints->ContainerPtrToValuePtr<void>(Scene.Get())),
            EmptyWorlds(Levels, Levels->ContainerPtrToValuePtr<void>(Scene.Get()));
        F.Check(TEXT("Actual native soft class/world arrays also empty"),
            EmptyClasses.Num() == 0 && EmptyWorlds.Num() == 0);
        F.Verdict(TEXT("Actual native explicit null readback"), Input,
            UP4AuthoringTools::NativeAssetReadback(Scene.Get(), Input), true);
    }
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}
// Private, unexecuted regression fragment. Append before the #endif in
// P4AuthoringRowsInspectionCoverageAutomation.cpp; reuses its authentic native
// acquisition/baseline fixture and independent complete native/source snapshots.
// Root is the sole canonical publisher/build owner.

namespace P4RowsBaseline163
{}

// Private unexecuted additive fragment. Append before the #endif in the
// inspection automation TU, reusing P4RowsCoverage138 fixture/state helpers.
// Actual UE4.26 engine classes/structs only; no schema or property-flag changes.
#include "Engine/PrimaryAssetLabel.h"
#include "AssetRegistry/AssetBundleData.h"

namespace P4RowsPublic182
{
bool N182AssetRefusal(P4RowsCoverage138::FN138Suite& F, UObject* Asset, const FString& Input)
{
    using namespace P4RowsCoverage138;
    FN138State Held(Asset, true);
    Held.Same(F);
    if (!F.Good)
        return false;
    F.Verdict(TEXT("Public native scalar/container canonical refusal"), Input,
        UP4AuthoringTools::NativeCanonicalAssetValues(Asset, Input), false);
    const bool Applied = UP4AuthoringTools::NativeApplyAssetValues(Asset, Input);
    F.Trace(TEXT("Actual public native apply refusal"), Input, Applied ? TEXT("true") : TEXT("false"));
    F.Check(TEXT("Invalid child cannot partially replace another native field"), !Applied);
    Held.Same(F);
    return !Applied;
}
} //namespace P4RowsPublic182

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows182PrimaryRulesScalarMatrix, "P4MapKit.Quality.Rows182PrimaryRulesScalarMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows182PrimaryRulesScalarMatrix::RunTest(const FString&)
{
    using namespace P4RowsCoverage138;
    using namespace P4RowsPublic182;
    FN138Suite F(*this, TEXT("PrimaryRulesScalarMatrix"));
    TStrongObjectPtr<UPrimaryAssetLabel> Asset(NewObject<UPrimaryAssetLabel>(
        CreatePackage(*(F.PackageRoot / TEXT("Rules"))), TEXT("Rules"), RF_Transient | RF_Transactional));
    auto Rules = CastField<FStructProperty>(Asset->GetClass()->FindPropertyByName(TEXT("Rules")));
    auto Collection = CastField<FStructProperty>(Asset->GetClass()->FindPropertyByName(TEXT("AssetCollection")));
    auto Priority = Rules ? CastField<FIntProperty>(Rules->Struct->FindPropertyByName(TEXT("Priority"))) : nullptr;
    auto Cook = Rules ? CastField<FEnumProperty>(Rules->Struct->FindPropertyByName(TEXT("CookRule"))) : nullptr;
    auto Recursive = Rules ? CastField<FBoolProperty>(Rules->Struct->FindPropertyByName(TEXT("bApplyRecursively")))
                           : nullptr;
    auto Name = Collection ? CastField<FNameProperty>(Collection->Struct->FindPropertyByName(TEXT("CollectionName")))
                           : nullptr;
    if (!F.Check(TEXT("Actual native rule int32/enum/bool and collection FName schemas"),
            Rules && Priority && Cook && Recursive && Name))
        return false;
    const FString Initial = TEXT(
        "{\"Rules\":{\"Priority\":2147483647,\"ChunkId\":-2147483648,\"bApplyRecursively\":false,\"CookRule\":\"AlwaysCook\"},\"bIsRuntimeLabel\":true,\"bLabelAssetsInMyDirectory\":false,\"AssetCollection\":{\"CollectionName\":\"RetainedCollection\"}}");
    if (!F.Check(TEXT("Public native scalar positive control"),
            UP4AuthoringTools::NativeApplyAssetValues(Asset.Get(), Initial)))
        return false;
    F.Check(TEXT("Independent native signed int32 endpoints, enum and sibling bitfields"),
        Asset->Rules.Priority == MAX_int32 && Asset->Rules.ChunkId == MIN_int32 && !Asset->Rules.bApplyRecursively &&
            Asset->Rules.CookRule == EPrimaryAssetCookRule::AlwaysCook && Asset->bIsRuntimeLabel &&
            !Asset->bLabelAssetsInMyDirectory);
    F.Check(TEXT("Independent native collection name"),
        Name->GetPropertyValue(Name->ContainerPtrToValuePtr<void>(
            Collection->ContainerPtrToValuePtr<void>(Asset.Get()))) == FName(TEXT("RetainedCollection")));
    F.Verdict(TEXT("Complete scalar native readback control"), Initial,
        UP4AuthoringTools::NativeAssetReadback(Asset.Get(), Initial), true);
    Asset->GetOutermost()->GetMetaData()->SetValue(
        Asset.Get(), TEXT("P4Rows138Sentinel"), TEXT("retain-all-rule-siblings"));
    Asset->GetOutermost()->SetDirtyFlag(false);
    for (const FString& Token :
        {FString(TEXT("null")), FString(TEXT("true")), FString(TEXT("\"1\"")), FString(TEXT("[]")), FString(TEXT("{}")),
            FString(TEXT("2147483648")), FString(TEXT("-2147483649")), FString(TEXT("1.0")), FString(TEXT("1e0"))})
        N182AssetRefusal(
            F, Asset.Get(), TEXT("{\"bIsRuntimeLabel\":false,\"Rules\":{\"Priority\":") + Token + TEXT("}}"));
    for (const FString& Token : {FString(TEXT("null")), FString(TEXT("0")), FString(TEXT("\"true\"")),
             FString(TEXT("[]")), FString(TEXT("{}"))})
    {
        N182AssetRefusal(
            F, Asset.Get(), TEXT("{\"Rules\":{\"Priority\":0,\"bApplyRecursively\":") + Token + TEXT("}}"));
        N182AssetRefusal(F, Asset.Get(), TEXT("{\"Rules\":{\"Priority\":0},\"bIsRuntimeLabel\":") + Token + TEXT("}"));
    }
    for (const FString& Token :
        {FString(TEXT("null")), FString(TEXT("0")), FString(TEXT("true")), FString(TEXT("\"UnknownNativeEnum\"")),
            FString(TEXT("\"AlwaysCook\\u0000tail\"")), N138Quote(FString::ChrN(NAME_SIZE, TEXT('E')))})
        N182AssetRefusal(F, Asset.Get(), TEXT("{\"Rules\":{\"Priority\":0,\"CookRule\":") + Token + TEXT("}}"));
    for (const FString& Token : {FString(TEXT("null")), FString(TEXT("1")), FString(TEXT("true")),
             FString(TEXT("\"Name\\u0000tail\"")), N138Quote(FString::ChrN(NAME_SIZE, TEXT('N')))})
        N182AssetRefusal(F, Asset.Get(),
            TEXT("{\"Rules\":{\"Priority\":0},\"AssetCollection\":{\"CollectionName\":") + Token + TEXT("}}"));
    for (const FString& Input : {FString(TEXT("{\"Rules\":null}")), FString(TEXT("{\"Rules\":[]}")),
             FString(TEXT("{\"Rules\":\"str\"}")), FString(TEXT("{\"Rules\":{\"UnknownNativeField\":7}}")),
             FString(TEXT("{\"AssetCollection\":{\"CollectionName\":\"A\",\"collectionname\":\"B\"}}"))})
        N182AssetRefusal(F, Asset.Get(), Input);
    const FString Partial = TEXT(
        "{\"Rules\":{\"Priority\":-2147483648,\"CookRule\":\"EPrimaryAssetCookRule::NeverCook\"},\"AssetCollection\":{\"CollectionName\":\"retainedcollection\"}}");
    if (!F.Check(TEXT("Valid partial rule edit and qualified enum spelling apply"),
            UP4AuthoringTools::NativeApplyAssetValues(Asset.Get(), Partial)))
        return false;
    F.Check(TEXT("Partial struct update retains all omitted native siblings"),
        Asset->Rules.Priority == MIN_int32 && Asset->Rules.ChunkId == MIN_int32 && !Asset->Rules.bApplyRecursively &&
            Asset->Rules.CookRule == EPrimaryAssetCookRule::NeverCook && Asset->bIsRuntimeLabel &&
            !Asset->bLabelAssetsInMyDirectory);
    F.Verdict(TEXT("Exact partial native rule/name readback"), Partial,
        UP4AuthoringTools::NativeAssetReadback(Asset.Get(), Partial), true);
    Asset->Rules.Priority = MIN_int32 + 1;
    Asset->GetOutermost()->SetDirtyFlag(false);
    FN138State Mutated(Asset.Get(), true);
    Mutated.Same(F);
    F.Verdict(TEXT("Actual native signed scalar drift refuses unchanged source"), Partial,
        UP4AuthoringTools::NativeAssetReadback(Asset.Get(), Partial), false);
    Mutated.Same(F);
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows182BundlePathStructMatrix, "P4MapKit.Quality.Rows182BundlePathStructMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows182BundlePathStructMatrix::RunTest(const FString&)
{
    using namespace P4RowsCoverage138;
    using namespace P4RowsPublic182;
    FN138Suite F(*this, TEXT("BundlePathStructMatrix"));
    TStrongObjectPtr<UPrimaryAssetLabel> Asset(NewObject<UPrimaryAssetLabel>(
        CreatePackage(*(F.PackageRoot / TEXT("Bundle"))), TEXT("Bundle"), RF_Transient | RF_Transactional));
    auto Data = CastField<FStructProperty>(Asset->GetClass()->FindPropertyByName(TEXT("AssetBundleData")));
    auto Bundles = Data ? CastField<FArrayProperty>(Data->Struct->FindPropertyByName(TEXT("Bundles"))) : nullptr;
    auto Entry = Bundles ? CastField<FStructProperty>(Bundles->Inner) : nullptr;
    auto Paths = Entry ? CastField<FArrayProperty>(Entry->Struct->FindPropertyByName(TEXT("BundleAssets"))) : nullptr;
    auto Path = Paths ? CastField<FStructProperty>(Paths->Inner) : nullptr;
    if (!F.Check(TEXT("Real constructed native bundle and SoftObjectPath struct schemas"),
            Data && Data->Struct->GetPathName() == TEXT("/Script/CoreUObject.AssetBundleData") && Path &&
                Path->Struct->GetPathName() == TEXT("/Script/CoreUObject.SoftObjectPath") &&
                Path->Struct->GetCppStructOps()))
        return false;
    const FString Identity = F.PackageRoot / TEXT("MissingAsset.MissingAsset");
    const FString StringInput =
        TEXT("{\"AssetBundleData\":{\"Bundles\":[{\"BundleName\":\"RetainedBundle\",\"BundleAssets\":[") +
        N138Quote(Identity) + TEXT(",\"\"]}]}}");
    if (!F.Check(TEXT("Plain and empty soft-path strings convert through public native struct admission"),
            UP4AuthoringTools::NativeApplyAssetValues(Asset.Get(), StringInput)))
        return false;
    auto Native = static_cast<FAssetBundleData*>(Data->ContainerPtrToValuePtr<void>(Asset.Get()));
    if (!F.Check(TEXT("Actual native bundle/path counts"),
            Native->Bundles.Num() == 1 && Native->Bundles[0].BundleAssets.Num() == 2))
        return false;
    F.Check(TEXT("Native soft struct path is exact and empty remains null"),
        Native->Bundles[0].BundleName == FName(TEXT("RetainedBundle")) &&
            Native->Bundles[0].BundleAssets[0].GetAssetPathName() == FName(*Identity) &&
            Native->Bundles[0].BundleAssets[0].GetSubPathString().IsEmpty() &&
            Native->Bundles[0].BundleAssets[1].IsNull());
    F.Verdict(TEXT("String-form native struct readback control"), StringInput,
        UP4AuthoringTools::NativeAssetReadback(Asset.Get(), StringInput), true);
    Asset->GetOutermost()->SetDirtyFlag(false);
    const TArray<FString> Invalid = {TEXT("null"), TEXT("false"), TEXT("17"), TEXT("[]"),
        TEXT("{\"UnknownNativePathField\":\"A\"}"), TEXT("{\"AssetPathName\":null}"), TEXT("{\"AssetPathName\":7}"),
        TEXT("{\"AssetPathName\":\"A\\u0000tail\"}"), TEXT("{\"AssetPathName\":\"None\",\"SubPathString\":false}"),
        TEXT("{\"AssetPathName\":\"None\",\"SubPathString\":\"A\\u0000tail\"}")};
    for (const auto& Bad : Invalid)
        N182AssetRefusal(F, Asset.Get(),
            TEXT(
                "{\"Rules\":{\"Priority\":7},\"AssetBundleData\":{\"Bundles\":[{\"BundleName\":\"Changed\",\"BundleAssets\":[") +
                N138Quote(Identity) + TEXT(",") + Bad + TEXT("]}]}}"));
    const FString
        SubPath = TEXT("Component.Child"),
        Structured =
            TEXT(
                "{\"AssetBundleData\":{\"Bundles\":[{\"BundleName\":\"retainedbundle\",\"BundleAssets\":[{\"AssetPathName\":") +
        N138Quote(Identity) + TEXT(",\"SubPathString\":") + N138Quote(SubPath) + TEXT("}]}]}}");
    if (!F.Check(TEXT("Exact explicit native path fields apply"),
            UP4AuthoringTools::NativeApplyAssetValues(Asset.Get(), Structured)))
        return false;
    if (!F.Check(TEXT("Structured native path has one real entry"),
            Native->Bundles.Num() == 1 && Native->Bundles[0].BundleAssets.Num() == 1))
        return false;
    F.Check(TEXT("Independent native FName and case-sensitive subpath fields"),
        Native->Bundles[0].BundleAssets[0].GetAssetPathName() == FName(*Identity) &&
            Native->Bundles[0].BundleAssets[0].GetSubPathString() == SubPath);
    F.Verdict(TEXT("Explicit native path fields retain exact readback"), Structured,
        UP4AuthoringTools::NativeAssetReadback(Asset.Get(), Structured), true);
    Native->Bundles[0].BundleAssets[0] = FSoftObjectPath(FName(*Identity), TEXT("Component.Changed"));
    Asset->GetOutermost()->SetDirtyFlag(false);
    FN138State Changed(Asset.Get(), true);
    Changed.Same(F);
    F.Verdict(TEXT("Native FString subpath drift refuses unchanged exact source"), Structured,
        UP4AuthoringTools::NativeAssetReadback(Asset.Get(), Structured), false);
    Changed.Same(F);
    if (!F.Check(TEXT("Legitimate empty native bundle list clears completely"),
            UP4AuthoringTools::NativeApplyAssetValues(Asset.Get(), TEXT("{\"AssetBundleData\":{\"Bundles\":[]}}"))))
        return false;
    F.Check(TEXT("Independent native bundle deletion retained"), Native->Bundles.Num() == 0);
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows182NativeNameStringTableMatrix,
    "P4MapKit.Quality.Rows182NativeNameStringTableMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows182NativeNameStringTableMatrix::RunTest(const FString&)
{
    using namespace P4RowsCoverage138;
    FN138Suite F(*this, TEXT("NativeNameStringTableMatrix"));
    auto Schema = LoadObject<UScriptStruct>(nullptr, TEXT("/Script/GameplayTags.GameplayTagTableRow"));
    auto Name = Schema ? CastField<FNameProperty>(Schema->FindPropertyByName(TEXT("Tag"))) : nullptr;
    auto String = Schema ? CastField<FStrProperty>(Schema->FindPropertyByName(TEXT("DevComment"))) : nullptr;
    if (!F.Check(TEXT("Actual constructed engine TableRowBase FName/FString schema"),
            Schema && Schema->IsChildOf(FTableRowBase::StaticStruct()) && Schema->GetCppStructOps() && Name && String))
        return false;
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(
        CreatePackage(*(F.PackageRoot / TEXT("Names"))), TEXT("Names"), RF_Transient | RF_Transactional));
    Table->RowStruct = Schema;
    const FString Comment = TEXT("Quote \" slash \\ newline\nUnicode \u2603 ") +
        FString::ChrN(NAME_SIZE + 8, TEXT('x'));
    const FString Initial = TEXT("[{\"Name\":\"RetainedRow\",\"Tag\":\"Fixture.Mixed.Tag\",\"DevComment\":") +
        N138Quote(Comment) + TEXT("}]");
    if (!F.Check(TEXT("Genuine native name/string rows import through public exact table API"),
            UP4AuthoringTools::ImportTableJson(Table.Get(), Initial)))
        return false;
    const auto Found = Table->GetRowMap().Find(TEXT("RetainedRow"));
    if (!F.Check(TEXT("Actual native named row exists"), Found != nullptr))
        return false;
    F.Check(TEXT("Every native FString code unit and FName identity is exact"),
        String->GetPropertyValue_InContainer(*Found) == Comment &&
            Name->GetPropertyValue_InContainer(*Found) == FName(TEXT("Fixture.Mixed.Tag")));
    const FString Alias = Initial.Replace(TEXT("Fixture.Mixed.Tag"), TEXT("fixture.mixed.tag"));
    F.Verdict(TEXT("Native FName identity permits genuine case aliases"), Alias,
        UP4AuthoringTools::NativeTableReadback(Table.Get(), Alias), true);
    Table->GetOutermost()->SetDirtyFlag(false);
    for (const FString& Child : {FString(TEXT("\"Tag\":null")), FString(TEXT("\"Tag\":1")),
             FString(TEXT("\"Tag\":false")), FString(TEXT("\"Tag\":\"A\\u0000tail\"")),
             TEXT("\"Tag\":") + N138Quote(FString::ChrN(NAME_SIZE, TEXT('T'))), FString(TEXT("\"DevComment\":null")),
             FString(TEXT("\"DevComment\":1")), FString(TEXT("\"DevComment\":false")),
             FString(TEXT("\"DevComment\":[]")), FString(TEXT("\"DevComment\":\"A\\u0000tail\""))})
    {
        const FString Input =
            TEXT(
                "[{\"Name\":\"ValidFirst\",\"Tag\":\"WouldChange\",\"DevComment\":\"first\"},{\"Name\":\"InvalidLate\",") +
            Child + TEXT("}]");
        FN138State Held(Table.Get());
        Held.Same(F);
        if (!F.Good)
            return false;
        F.Verdict(TEXT("Malformed actual native name/string table field refuses"), Input,
            UP4AuthoringTools::NativeCanonicalTableRows(Table.Get(), Input), false);
        const bool Imported = UP4AuthoringTools::ImportTableJson(Table.Get(), Input);
        F.Trace(TEXT("Actual late invalid native row import"), Input, Imported ? TEXT("true") : TEXT("false"));
        F.Check(TEXT("Earlier valid row never partially replaces original native rows"), !Imported);
        Held.Same(F);
    }
    String->SetPropertyValue_InContainer(*Found, Comment + TEXT("CaseSensitiveChange"));
    Table->GetOutermost()->SetDirtyFlag(false);
    FN138State Changed(Table.Get());
    Changed.Same(F);
    F.Verdict(TEXT("Actual native FString drift refuses unchanged source"), Initial,
        UP4AuthoringTools::NativeTableReadback(Table.Get(), Initial), false);
    Changed.Same(F);
    if (!F.Check(TEXT("Native table delete-all remains supported"),
            UP4AuthoringTools::ImportTableJson(Table.Get(), TEXT("[]"))))
        return false;
    F.Check(TEXT("All native rows are actually deleted"), Table->GetRowMap().Num() == 0);
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

// PRIVATE, uncompiled/unexecuted. Append before #endif in the inspection test TU.
// Reuses P4RowsCoverage138 and its complete, primed property-copy snapshot.
// Exact full163 gaps: Rows389:73/113,486:77,682:77/113 (uint16/uint32),
// Rows437:26 (FString map keys), Rows691-697 (native FText writes).
// Actual engine/recovered native schemas only; no reflected flags are changed.
#include "Engine/IntSerialization.h"
#include "Sound/SoundWave.h"
#include "Internationalization/Text.h"

namespace P4RowsNext201TypedTransport
{
bool N201Private(UObject* Object, const FString& Input, FString& Canonical, FString& Error)
{
    return P4PrepareExactPrivateProperties(Object, Input, Canonical, Error);
}
void N201PrivateRefusal(P4RowsCoverage138::FN138Suite& F, UObject* Object, const FString& Input)
{
    using namespace P4RowsCoverage138;
    FN138State Held(Object, true);
    Held.Same(F);
    if (!F.Good)
        return;
    FString Canonical, Error;
    const bool Accepted = N201Private(Object, Input, Canonical, Error);
    F.Trace(TEXT("Actual private typed import refusal"), Input,
        FString(Accepted ? TEXT("true; ") : TEXT("false; ")) + Error);
    F.Check(TEXT("Invalid exact value refuses before any scratch field is changed"), !Accepted && !Error.IsEmpty());
    Held.Same(F);
}
struct FN201Integer
{
    const TCHAR* Name;
    const TCHAR* Kind;
    const TCHAR* Low;
    const TCHAR* High;
    const TCHAR* Below;
    const TCHAR* Above;
    bool Unsigned;
    int32 Width;
    uint64 LowBits, HighBits;
};
bool N201ActualInteger(FNumericProperty* Field, UObject* Owner, bool Unsigned, uint64 Expected)
{
    const void* Storage = Field->ContainerPtrToValuePtr<void>(Owner);
    return Unsigned ? Field->GetUnsignedIntPropertyValue(Storage) == Expected
                    : uint64(Field->GetSignedIntPropertyValue(Storage)) == Expected;
}
FString N201OneField(const TCHAR* Field, const FString& Value)
{
    return TEXT("{") + P4RowsCoverage138::N138Quote(Field) + TEXT(":") + Value + TEXT("}");
}
} //namespace P4RowsNext201TypedTransport

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows201PrivateIntegerWidths, "P4MapKit.Quality.Rows201PrivateIntegerWidths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows201PrivateIntegerWidths::RunTest(const FString&)
{
    using namespace P4RowsCoverage138;
    using namespace P4RowsNext201TypedTransport;
    FN138Suite F(*this, TEXT("PrivateIntegerWidths"));
    TStrongObjectPtr<UIntSerialization> Object(
        NewObject<UIntSerialization>(GetTransientPackage(), NAME_None, RF_Transient));
    Object->UnsignedInt8Variable = 17;
    Object->UnsignedInt16Variable = 1234;
    Object->UnsignedInt32Variable = 4000000001u;
    Object->UnsignedInt64Variable = 9007199254740993ull;
    Object->SignedInt8Variable = -19;
    Object->SignedInt16Variable = -123;
    Object->SignedInt32Variable = -123456789;
    Object->SignedInt64Variable = -9007199254740993ll;
    const TArray<FN201Integer> Fields = {{TEXT("UnsignedInt8Variable"), TEXT("ByteProperty"), TEXT("0"), TEXT("255"),
                                             TEXT("-1"), TEXT("256"), true, 8, 0ull, 255ull},
        {TEXT("UnsignedInt16Variable"), TEXT("UInt16Property"), TEXT("0"), TEXT("65535"), TEXT("-1"), TEXT("65536"),
            true, 16, 0ull, 65535ull},
        {TEXT("UnsignedInt32Variable"), TEXT("UInt32Property"), TEXT("0"), TEXT("4294967295"), TEXT("-1"),
            TEXT("4294967296"), true, 32, 0ull, 4294967295ull},
        {TEXT("UnsignedInt64Variable"), TEXT("UInt64Property"), TEXT("0"), TEXT("18446744073709551615"), TEXT("-1"),
            TEXT("18446744073709551616"), true, 64, 0ull, MAX_uint64},
        {TEXT("SignedInt8Variable"), TEXT("Int8Property"), TEXT("-128"), TEXT("127"), TEXT("-129"), TEXT("128"), false,
            8, MAX_uint64 - 127ull, 127ull},
        {TEXT("SignedInt16Variable"), TEXT("Int16Property"), TEXT("-32768"), TEXT("32767"), TEXT("-32769"),
            TEXT("32768"), false, 16, MAX_uint64 - 32767ull, 32767ull},
        {TEXT("SignedInt32Variable"), TEXT("IntProperty"), TEXT("-2147483648"), TEXT("2147483647"), TEXT("-2147483649"),
            TEXT("2147483648"), false, 32, MAX_uint64 - 2147483647ull, 2147483647ull},
        {TEXT("SignedInt64Variable"), TEXT("Int64Property"), TEXT("-9223372036854775808"), TEXT("9223372036854775807"),
            TEXT("-9223372036854775809"), TEXT("9223372036854775808"), false, 64, uint64(MAX_int64) + 1ull,
            uint64(MAX_int64)}};
    for (const auto& Case : Fields)
    {
        auto Field = CastField<FNumericProperty>(Object->GetClass()->FindPropertyByName(Case.Name));
        if (!F.Check(TEXT("Installed native integer type/width without invented permissions"),
                Field && Field->GetClass()->GetName() == Case.Kind && Field->ElementSize * 8 == Case.Width &&
                    !Field->GetIntPropertyEnum()))
            return false;
        const EPropertyFlags OriginalFlags = Field->GetPropertyFlags();
        for (int32 Endpoint = 0; Endpoint < 2; ++Endpoint)
        {
            const FString Token = Endpoint ? Case.High : Case.Low;
            const uint64 Bits = Endpoint ? Case.HighBits : Case.LowBits;
            FN138State Before(Object.Get(), true);
            Before.Same(F);
            if (!F.Good)
                return false;
            FString Canonical, Error;
            const FString Input = N201OneField(Case.Name, Token);
            const bool Accepted = N201Private(Object.Get(), Input, Canonical, Error);
            F.Trace(TEXT("Actual privileged native width endpoint"), Input,
                FString(Accepted ? TEXT("true; ") : TEXT("false; ")) + Error);
            if (!F.Check(TEXT("Existing private import admits exact native width endpoint"), Accepted))
                return false;
            F.Check(TEXT("Independent native typed integer getter retains every bit"),
                N201ActualInteger(Field, Object.Get(), Case.Unsigned, Bits));
            auto Parsed = N138Parse(Canonical);
            FString ActualToken;
            const auto Value = Parsed.IsValid() ? Parsed->TryGetField(Case.Name) : nullptr;
            F.Check(TEXT("Source JSON number type and exact decimal spelling retained"),
                Value.IsValid() && Value->Type == EJson::Number && Value->TryGetString(ActualToken) &&
                    ActualToken == Token);
            for (const auto& Copy : Before.PropertyCopies)
                if (Copy->Property != Field)
                    for (int32 Slot = 0; Slot < Copy->Property->ArrayDim; ++Slot)
                        F.Check(TEXT("Every initialized integer sibling retained during valid private edit"),
                            Copy->Same(Object.Get(), Slot));
            F.Check(TEXT("Private edit retains identity/flags/dirty state and emits no editor callbacks"),
                FindObject<UObject>(nullptr, *Before.Path) == Object.Get() && Object->GetFlags() == Before.Flags &&
                    Object->GetOutermost()->IsDirty() == Before.Dirty && Before.Pre == 0 && Before.Post == 0);
        }
        const TCHAR* Sibling = Field->GetFName() == TEXT("SignedInt32Variable") ? TEXT("UnsignedInt8Variable")
                                                                                : TEXT("SignedInt32Variable");
        for (const FString& Token : {FString(Case.Below), FString(Case.Above), FString(TEXT("null")),
                 FString(TEXT("true")), FString(TEXT("\"1\"")), FString(TEXT("[]")), FString(TEXT("{}")),
                 FString(TEXT("1.0")), FString(TEXT("1e0"))})
            N201PrivateRefusal(F, Object.Get(),
                TEXT("{") + N138Quote(Sibling) + TEXT(":7,") + N138Quote(Case.Name) + TEXT(":") + Token + TEXT("}"));
        F.Check(TEXT("Original native integer property permissions remain unchanged"),
            Field->GetPropertyFlags() == OriginalFlags);
    }
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows201OriginalStringMapIdentity,
    "P4MapKit.Quality.Rows201OriginalStringMapIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows201OriginalStringMapIdentity::RunTest(const FString&)
{
    using namespace P4RowsCoverage138;
    FN138Suite F(*this, TEXT("OriginalStringMapIdentity"));
    auto Schema = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.TutorialGuideTable"));
    auto Map = Schema ? CastField<FMapProperty>(Schema->FindPropertyByName(TEXT("HighlightGuideList"))) : nullptr;
    auto Key = Map ? CastField<FStrProperty>(Map->KeyProp) : nullptr;
    auto Value = Map ? CastField<FStructProperty>(Map->ValueProp) : nullptr;
    auto Quest = Value ? CastField<FNameProperty>(Value->Struct->FindPropertyByName(TEXT("QuestName"))) : nullptr;
    auto Mode = Schema ? CastField<FEnumProperty>(Schema->FindPropertyByName(TEXT("Mode"))) : nullptr;
    if (!F.Check(TEXT("Original native TableRowBase FString/HighlightGuideParam map schema"),
            Schema && Schema->GetCppStructOps() && Schema->IsChildOf(FTableRowBase::StaticStruct()) && Key && Quest &&
                Mode && Mode->GetEnum() &&
                Value->Struct->GetPathName() == TEXT("/Script/Carrot4.HighlightGuideParam") &&
                Mode->GetEnum()->NumEnums() > 0))
        return false;
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(
        CreatePackage(*(F.PackageRoot / TEXT("Tutorial"))), TEXT("Tutorial"), RF_Transient | RF_Transactional));
    Table->RowStruct = Schema;
    const FString Prefix = TEXT("[{\"Name\":\"Retained\",\"Mode\":") +
        N138Quote(Mode->GetEnum()->GetNameStringByIndex(0)) + TEXT(",\"GuideList\":[],\"HighlightGuideList\":");
    const FString NativeKey(TEXT("Case")), CaseAlias(TEXT("case"));
    if (!F.Check(TEXT("Actual native FString key equality/hash establish a case alias without losing stored spelling"),
            Key->Identical(&NativeKey, &CaseAlias, PPF_None) &&
                Key->GetValueTypeHash(&NativeKey) == Key->GetValueTypeHash(&CaseAlias) &&
                !NativeKey.Equals(CaseAlias, ESearchCase::CaseSensitive)))
        return false;
    const FString Pairs = TEXT(
        "[[\"Case\",{\"QuestName\":\"UpperQuest\"}],[\"Other\",{\"QuestName\":\"OtherQuest\"}]]");
    const FString Initial = Prefix + Pairs + TEXT("}]");
    if (!F.Check(TEXT("Genuine distinct native string-map import control"),
            UP4AuthoringTools::ImportTableJson(Table.Get(), Initial)))
        return false;
    uint8* const Row = Table->GetRowMap().FindChecked(TEXT("Retained"));
    FScriptMapHelper Native(Map, Map->ContainerPtrToValuePtr<void>(Row));
    F.Check(TEXT("Two distinct native string keys both survive"), Native.Num() == 2);
    int32 Exact = 0;
    TMap<int32, FString> StoredKeySpellings;
    for (int32 Index = 0; Index < Native.GetMaxIndex(); ++Index)
        if (Native.IsValidIndex(Index))
        {
            const FString Identity = Key->GetPropertyValue(Native.GetKeyPtr(Index));
            const FName NativeQuest = Quest->GetPropertyValue(
                Quest->ContainerPtrToValuePtr<void>(Native.GetValuePtr(Index)));
            StoredKeySpellings.Add(Index, Identity);
            if ((Identity.Equals(TEXT("Case"), ESearchCase::CaseSensitive) && NativeQuest == TEXT("UpperQuest")) ||
                (Identity.Equals(TEXT("Other"), ESearchCase::CaseSensitive) && NativeQuest == TEXT("OtherQuest")))
                ++Exact;
        }
    F.Check(TEXT("Independent native string keys and struct values are exact"), Exact == 2);
    const auto SameStoredKeys = [&]()
    {
        const uint8* const* Live = Table->GetRowMap().Find(TEXT("Retained"));
        if (!Live || *Live != Row)
            return false;
        FScriptMapHelper Current(Map, Map->ContainerPtrToValuePtr<void>(*Live));
        if (Current.Num() != StoredKeySpellings.Num())
            return false;
        for (const auto& Stored : StoredKeySpellings)
        {
            if (!Current.IsValidIndex(Stored.Key))
                return false;
            const FString Actual = Key->GetPropertyValue(Current.GetKeyPtr(Stored.Key));
            if (!Actual.Equals(Stored.Value, ESearchCase::CaseSensitive) || Actual.Len() != Stored.Value.Len() ||
                FMemory::Memcmp(*Actual, *Stored.Value, SIZE_T(Actual.Len()) * sizeof(TCHAR)) != 0)
                return false;
        }
        return true;
    };
    Table->GetOutermost()->SetDirtyFlag(false);
    {
        FN138State Held(Table.Get());
        Held.Same(F);
        if (!F.Good)
            return false;
        const FString Reordered = Prefix +
            TEXT(
                "{\"__native_map_pairs\":[[\"Other\",{\"QuestName\":\"otherquest\"}],[\"Case\",{\"QuestName\":\"upperquest\"}]]}") +
            TEXT("}]");
        F.Verdict(TEXT("Pair order and native FName value casing do not change map identity"), Reordered,
            UP4AuthoringTools::NativeTableReadback(Table.Get(), Reordered), true);
        Held.Same(F);
        for (const FString& Bad :
            {FString(TEXT("[[\"Case\",{\"QuestName\":\"One\"}],[\"Case\",{\"QuestName\":\"Two\"}]]")),
                FString(TEXT("[[\"Case\",{\"QuestName\":\"One\"}],[\"case\",{\"QuestName\":\"Two\"}]]")),
                FString(TEXT("[[7,{\"QuestName\":\"One\"}]]")), FString(TEXT("[[false,{\"QuestName\":\"One\"}]]")),
                FString(TEXT("[[null,{\"QuestName\":\"One\"}]]")), FString(TEXT("[[\"Case\",{\"QuestName\":null}]]")),
                FString(TEXT("[[\"Case\",{\"QuestName\":\"One\"}],[\"Other\",{\"QuestName\":7}]]")),
                FString(TEXT("[[\"Case\\u0000tail\",{\"QuestName\":\"One\"}]]"))})
        {
            const FString Input = Prefix + Bad + TEXT("}]");
            const bool Applied = UP4AuthoringTools::ImportTableJson(Table.Get(), Input);
            F.Trace(TEXT("Actual native string-map import refusal"), Input, Applied ? TEXT("true") : TEXT("false"));
            F.Check(TEXT("Invalid typed native string-map import explicitly refuses"), !Applied);
            F.Check(TEXT("Malformed or duplicate exact FString key cannot replace any prior row"),
                UP4AuthoringTools::NativeTableReadback(Table.Get(), Input).Contains(TEXT("\"valid\":false")));
            Held.Same(F);
            F.Check(TEXT("Every native string key retains its exact code units and spelling after refusal"),
                SameStoredKeys());
        }
        // Native key identity is case insensitive, but source spelling is data.
        // Readback must detect that drift without rewriting either stored key.
        const FString Alias = Prefix +
            TEXT("[[\"CASE\",{\"QuestName\":\"UpperQuest\"}],[\"Other\",{\"QuestName\":\"OtherQuest\"}]]") + TEXT("}]");
        const FString AliasResult = UP4AuthoringTools::NativeTableReadback(Table.Get(), Alias);
        F.Verdict(TEXT("Native FString key spelling drift explicitly refuses"), Alias, AliasResult, false);
        Held.Same(F);
        const auto AliasReadback = N138Parse(AliasResult);
        FString AliasError;
        F.Check(TEXT("String-map refusal identifies exact key-payload drift"),
            AliasReadback.IsValid() && AliasReadback->TryGetStringField(TEXT("error"), AliasError) &&
                AliasError.Equals(TEXT("Retained.HighlightGuideList.key: native import changed field value"),
                    ESearchCase::CaseSensitive));
        F.Check(TEXT("Read-only case alias preserves every exact stored native key byte"), SameStoredKeys());
        const FString DifferentKey = Prefix +
            TEXT("[[\"Changed\",{\"QuestName\":\"UpperQuest\"}],[\"Other\",{\"QuestName\":\"OtherQuest\"}]]") +
            TEXT("}]");
        F.Verdict(TEXT("A genuinely different native FString key is detected as key drift"), DifferentKey,
            UP4AuthoringTools::NativeTableReadback(Table.Get(), DifferentKey), false);
        Held.Same(F);
        F.Check(TEXT("Read-only distinct-key refusal preserves every exact stored native key byte"), SameStoredKeys());
    }
    if (!F.Check(TEXT("Explicit empty source clears native map without deleting row identity"),
            UP4AuthoringTools::ImportTableJson(Table.Get(), Prefix + TEXT("[]}]"))))
        return false;
    F.Check(TEXT("Native map deletion is complete and outer row still exists"),
        Table->GetRowMap().Num() == 1 &&
            FScriptMapHelper(Map, Map->ContainerPtrToValuePtr<void>(Table->GetRowMap().FindChecked(TEXT("Retained"))))
                    .Num() == 0);
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Rows201PrivateSubtitleTextMatrix,
    "P4MapKit.Quality.Rows201PrivateSubtitleTextMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Rows201PrivateSubtitleTextMatrix::RunTest(const FString&)
{
    using namespace P4RowsCoverage138;
    using namespace P4RowsNext201TypedTransport;
    FN138Suite F(*this, TEXT("PrivateSubtitleTextMatrix"));
    TStrongObjectPtr<USoundWave> Wave(NewObject<USoundWave>(GetTransientPackage(), NAME_None, RF_Transient));
    auto Array = CastField<FArrayProperty>(Wave->GetClass()->FindPropertyByName(TEXT("Subtitles")));
    auto Cue = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
    auto Text = Cue ? CastField<FTextProperty>(Cue->Struct->FindPropertyByName(TEXT("Text"))) : nullptr;
    auto Time = Cue ? CastField<FFloatProperty>(Cue->Struct->FindPropertyByName(TEXT("Time"))) : nullptr;
    if (!F.Check(TEXT("Installed native subtitle array with actual FText and float members"),
            Text && Time && Cue->Struct->GetPathName() == TEXT("/Script/Engine.SubtitleCue") &&
                Cue->Struct->GetCppStructOps()))
        return false;
    const FString Localized = TEXT(
        "{\"__native_text\":{\"flags\":0,\"history\":0,\"namespace\":\"Rows201.Subtitles\",\"key\":\"StableSubtitleKey\",\"source\":\"Source subtitle\"}}");
    const FString Invariant = TEXT(
        "{\"__native_text\":{\"flags\":2,\"history\":-1,\"has_source\":true,\"source\":\"Invariant subtitle\"}}");
    const FString Empty = TEXT("{\"__native_text\":{\"flags\":0,\"history\":-1,\"has_source\":false}}");
    FString Canonical, Error;
    const FString Initial = TEXT("{\"Subtitles\":[{\"Time\":1.25,\"Text\":") + Localized +
        TEXT("},{\"Time\":2.5,\"Text\":") + Invariant + TEXT("},{\"Time\":3.75,\"Text\":") + Empty + TEXT("}]}");
    const bool Accepted = N201Private(Wave.Get(), Initial, Canonical, Error);
    F.Trace(TEXT("Actual nested native subtitle/FText staging control"), Initial,
        FString(Accepted ? TEXT("true; ") : TEXT("false; ")) + Error);
    if (!F.Check(TEXT("Existing private staging retains all three qualified native text histories"),
            Accepted && Wave->Subtitles.Num() == 3))
        return false;
    const auto& A = Wave->Subtitles[0];
    const auto& B = Wave->Subtitles[1];
    const auto& C = Wave->Subtitles[2];
    F.Check(TEXT("Independent native subtitle times retain exact binary fractions"),
        A.Time == 1.25f && B.Time == 2.5f && C.Time == 3.75f);
    const auto Namespace = FTextInspector::GetNamespace(A.Text), Key = FTextInspector::GetKey(A.Text);
    const auto Source = FTextInspector::GetSourceString(A.Text),
               InvariantSource = FTextInspector::GetSourceString(B.Text);
    F.Check(TEXT("Native localization namespace/key/source survive native FText write"),
        Namespace.IsSet() && Key.IsSet() && Source && Namespace.GetValue() == TEXT("Rows201.Subtitles") &&
            Key.GetValue() == TEXT("StableSubtitleKey") && *Source == TEXT("Source subtitle"));
    F.Check(TEXT("Native invariant flag/source and empty history remain distinct"),
        B.Text.IsCultureInvariant() && InvariantSource && *InvariantSource == TEXT("Invariant subtitle") &&
            C.Text.IsEmpty() && !C.Text.IsCultureInvariant());
    for (const FString& Bad : {FString(TEXT("null")), FString(TEXT("\"flattened display text\"")),
             FString(TEXT("true")), Localized.Replace(TEXT("\"flags\":0"), TEXT("\"flags\":8")),
             Localized.Replace(TEXT("\"history\":0"), TEXT("\"history\":1")),
             Localized.Replace(TEXT("StableSubtitleKey"), TEXT("")),
             Localized.Replace(TEXT("Source subtitle"), TEXT("")),
             Invariant.Replace(TEXT("\"has_source\":true"), TEXT("\"has_source\":\"true\"")),
             Localized.Replace(TEXT("Source subtitle"), TEXT("Source\\u0000subtitle"))})
        N201PrivateRefusal(F, Wave.Get(),
            TEXT("{\"Subtitles\":[{\"Time\":7.5,\"Text\":") + Localized + TEXT("},{\"Time\":2.5,\"Text\":") + Bad +
                TEXT("}]}"));
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

// PRIVATE UNCOMPILED/UNEXECUTED. Append within the current RowsInspection
// automation guard after P4RowsCoverage138 fixture definitions, not SaveCoverage.
#include "P4TextValues.h"
#include "Serialization/MemoryWriter.h"
#include "Internationalization/TextNamespaceUtil.h"
namespace P4NativeTextEnvelopeCoverage
{
using namespace P4RowsCoverage138;
FString Localized(const FString& Namespace = TEXT("P4TextMatrix.UI"), const FString& Key = TEXT("RetainedKey"),
    const FString& Source = TEXT("Retained original source"))
{
    return TEXT("{\"__native_text\":{\"flags\":0,\"history\":0,\"namespace\":") + N138Quote(Namespace) +
        TEXT(",\"key\":") + N138Quote(Key) + TEXT(",\"source\":") + N138Quote(Source) + TEXT("}}");
}
FString Row(const TCHAR* Name, const FString& Text)
{
    return TEXT("{\"Name\":") + N138Quote(Name) + TEXT(",\"label\":") + Text + TEXT("}");
}
struct FFixture
{
    FN138Suite& F;
    TStrongObjectPtr<UDataTable> Table;
    FTextProperty* Property = nullptr;
    FString Initial;
    explicit FFixture(FN138Suite& In) : F(In) {}
    bool Begin()
    {
        auto Schema = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.HowToPlayOsusowakeTextLabel"));
        Property = Schema ? CastField<FTextProperty>(Schema->FindPropertyByName(TEXT("label"))) : nullptr;
        if (!F.Check(TEXT("actual original constructed native row and FText field"),
                Schema && Schema->IsChildOf(FTableRowBase::StaticStruct()) && Schema->GetCppStructOps() && Property))
            return false;
        Table.Reset(NewObject<UDataTable>(
            CreatePackage(*(F.PackageRoot / TEXT("Text"))), TEXT("Text"), RF_Transient | RF_Transactional));
        Table->RowStruct = Schema;
        Table->GetOutermost()->GetMetaData()->SetValue(
            Table.Get(), TEXT("P4Rows138Sentinel"), TEXT("native-text-envelope-owner"));
        Initial = TEXT("[") + Row(TEXT("Retained"), Localized()) + TEXT(",") +
            Row(TEXT("Sibling"), Localized(TEXT("P4TextMatrix.UI"), TEXT("SiblingKey"), TEXT("Independent sibling"))) +
            TEXT("]");
        if (!F.Check(TEXT("genuine original localized native rows establish independent held state"),
                UP4AuthoringTools::ImportTableJson(Table.Get(), Initial)))
            return false;
        Table->GetOutermost()->SetDirtyFlag(false);
        return true;
    }
    FText Text(const TCHAR* Name) const
    {
        return Property->GetPropertyValue_InContainer(Table->GetRowMap().FindChecked(FName(Name)));
    }
};
struct FTextState
{
    FText Value;
    TArray<uint8> Bytes;
    uint32 Flags;
    FString Display, Source, Namespace, Key;
    bool HasSource, HasNamespace, HasKey, Ready = false;
    explicit FTextState(const FText& In) : Value(In), Flags(FTextInspector::GetFlags(In)), Display(In.ToString())
    {
        const auto SourcePtr = FTextInspector::GetSourceString(In);
        HasSource = SourcePtr != nullptr;
        if (HasSource)
            Source = *SourcePtr;
        const auto Ns = FTextInspector::GetNamespace(In), K = FTextInspector::GetKey(In);
        HasNamespace = Ns.IsSet();
        HasKey = K.IsSet();
        if (HasNamespace)
            Namespace = Ns.GetValue();
        if (HasKey)
            Key = K.GetValue();
        FMemoryWriter Writer(Bytes);
        Writer.SetPortFlags(PPF_DuplicateVerbatim);
        FText Copy(In);
        Writer << Copy;
        Ready = !Writer.IsError() && Bytes.Num() > 0;
    }
    bool Same(const FText& In) const
    {
        const FTextState Now(In);
        return Ready && Now.Ready && Now.Bytes == Bytes && Now.Flags == Flags && Now.Display == Display &&
            Now.Source == Source && Now.Namespace == Namespace && Now.Key == Key && Now.HasSource == HasSource &&
            Now.HasNamespace == HasNamespace && Now.HasKey == HasKey;
    }
};
bool Refuse(FFixture& Fixture, const FString& Child)
{
    auto& F = Fixture.F;
    // A valid earlier row proposes a real change, then a later malformed label.
    const FString Input = TEXT("[") +
        Row(TEXT("WouldReplace"),
            Localized(TEXT("P4TextMatrix.UI"), TEXT("WouldReplaceKey"), TEXT("Must never publish"))) +
        TEXT(",") + Row(TEXT("InvalidLate"), Child) + TEXT("]");
    FN138State Held(Fixture.Table.Get());
    const FTextState Text(Fixture.Text(TEXT("Retained"))), Sibling(Fixture.Text(TEXT("Sibling")));
    if (!F.Check(TEXT("both independent held native FText serializations succeed and contain bytes"),
            Text.Ready && Sibling.Ready))
        return false;
    auto Package = Fixture.Table->GetOutermost();
    const uint32 Flags = Package->GetPackageFlags();
    const FGuid Guid = Package->GetGuid(), Persistent = Package->GetPersistentGuid();
    const FName Filename = Package->FileName;
    const uint64 Size = Package->FileSize;
    Held.Same(F);
    if (!F.Good)
        return false;
    F.Verdict(TEXT("malformed runtime text envelope refuses exact public admission"), Input,
        UP4AuthoringTools::NativeCanonicalTableRows(Fixture.Table.Get(), Input), false);
    const bool Imported = UP4AuthoringTools::ImportTableJson(Fixture.Table.Get(), Input);
    F.Trace(TEXT("actual late-text refusal"), Input, Imported ? TEXT("true") : TEXT("false"));
    F.Check(TEXT("valid earlier row never partially replaces original native text rows"), !Imported);
    Held.Same(F);
    F.Check(TEXT("both independent native FText serialization/localization/source/flags remain exact"),
        Text.Same(Fixture.Text(TEXT("Retained"))) && Sibling.Same(Fixture.Text(TEXT("Sibling"))));
    F.Check(TEXT("refusal preserves complete additional native package flags/identities/file state"),
        Package->GetPackageFlags() == Flags && Package->GetGuid() == Guid &&
            Package->GetPersistentGuid() == Persistent && Package->FileName == Filename && Package->FileSize == Size);
    return F.Good;
}
} //namespace P4NativeTextEnvelopeCoverage

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeTextEnvelopeKinds, "P4MapKit.Quality.NativeTextEnvelopeKinds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeTextEnvelopeKinds::RunTest(const FString&)
{
    using namespace P4NativeTextEnvelopeCoverage;
    FN138Suite F(*this, TEXT("NativeTextEnvelopeKinds"));
    FFixture Fixture(F);
    if (!Fixture.Begin())
        return false;
    const FString Base = Localized();
    const TArray<FString> Invalid{TEXT("null"), TEXT("false"), TEXT("[]"), TEXT("{}"), TEXT("{\"other\":{}}"),
        TEXT("{\"__native_text\":null}"), TEXT("{\"__native_text\":[]}"), TEXT("{\"__native_text\":false}"),
        Base.LeftChop(1) + TEXT(",\"extra_outer\":0}"), Base.Replace(TEXT("\"flags\":0,"), TEXT("")),
        Base.Replace(TEXT("\"flags\":0"), TEXT("\"flags\":\"0\"")),
        Base.Replace(TEXT("\"flags\":0"), TEXT("\"flags\":false")),
        Base.Replace(TEXT("\"flags\":0"), TEXT("\"flags\":0.5")),
        Base.Replace(TEXT("\"flags\":0"), TEXT("\"flags\":9223372036854775808")),
        Base.Replace(TEXT("\"history\":0,"), TEXT("")), Base.Replace(TEXT("\"history\":0"), TEXT("\"history\":\"0\"")),
        Base.Replace(TEXT("\"history\":0"), TEXT("\"history\":false")),
        Base.Replace(TEXT("\"history\":0"), TEXT("\"history\":-9223372036854775809"))};
    for (const auto& Child : Invalid)
        if (!Refuse(Fixture, Child))
            return false;
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeTextMetadataRefusals, "P4MapKit.Quality.NativeTextMetadataRefusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeTextMetadataRefusals::RunTest(const FString&)
{
    using namespace P4NativeTextEnvelopeCoverage;
    FN138Suite F(*this, TEXT("NativeTextMetadataRefusals"));
    FFixture Fixture(F);
    if (!Fixture.Begin())
        return false;
    const FString Base = Localized();
    TArray<FString> Invalid{
        Base.Replace(TEXT("\"namespace\":\"P4TextMatrix.UI\""), TEXT("\"missing_namespace\":\"P4TextMatrix.UI\"")),
        Base.Replace(TEXT("\"namespace\":\"P4TextMatrix.UI\""), TEXT("\"namespace\":null")),
        Base.Replace(TEXT("\"key\":\"RetainedKey\""), TEXT("\"missing_key\":\"RetainedKey\"")),
        Base.Replace(TEXT("\"key\":\"RetainedKey\""), TEXT("\"key\":false")),
        Base.Replace(
            TEXT("\"source\":\"Retained original source\""), TEXT("\"missing_source\":\"Retained original source\"")),
        Base.Replace(TEXT("\"source\":\"Retained original source\""), TEXT("\"source\":[]")),
        Localized(TEXT("P4TextMatrix.UI ")), Localized(TEXT("P4TextMatrix.UI[PackageSuffix]")),
        Base.Replace(TEXT("RetainedKey"), TEXT("A\\u0000tail")),
        Base.Replace(TEXT("Retained original source"), TEXT("A\\u0000tail")),
        Base.LeftChop(2) + TEXT(",\"unexpected_inner\":0}}"),
        TEXT("{\"__native_text\":{\"flags\":3,\"history\":-1,\"has_source\":false}}"),
        TEXT("{\"__native_text\":{\"flags\":0,\"history\":-1}}"),
        TEXT("{\"__native_text\":{\"flags\":0,\"history\":-1,\"has_source\":1}}"),
        TEXT("{\"__native_text\":{\"flags\":0,\"history\":-1,\"has_source\":true,\"source\":\"WouldLoseIdentity\"}}"),
        TEXT("{\"__native_text\":{\"flags\":2,\"history\":-1,\"has_source\":true,\"source\":\"value\",\"extra\":0}}"),
        TEXT("{\"__native_text\":{\"flags\":2,\"history\":-1,\"has_source\":true,\"missing_source\":\"value\"}}"),
        TEXT("{\"__native_text\":{\"flags\":2,\"history\":-1,\"has_source\":true,\"source\":false}}"),
        TEXT("{\"__native_text\":{\"flags\":2,\"history\":-1,\"has_source\":true,\"source\":\"\"}}"),
        TEXT("{\"__native_text\":{\"flags\":0,\"history\":-1,\"has_source\":false,\"source\":\"MustNotDiscard\"}}")};
    for (const auto& Child : Invalid)
        if (!Refuse(Fixture, Child))
            return false;
    // Legitimate ] without [ is an ordinary namespace, not a package suffix.
    const FString Source = TEXT("Quoted \" source \\ slash\nUnicode \u2603"), Namespace(TEXT("P4TextMatrix.UI]")),
                  Key(TEXT("EscapedKey"));
    const FString Valid = TEXT("[") + Row(TEXT("Retained"), Localized(Namespace, Key, Source)) + TEXT(",") +
        Row(TEXT("Sibling"), Localized(TEXT("P4TextMatrix.UI"), TEXT("SiblingKey"), TEXT("Independent sibling"))) +
        TEXT("]");
    const FTextState Sibling(Fixture.Text(TEXT("Sibling")));
    if (!F.Check(
            TEXT("positive control's held native sibling serialization succeeds and contains bytes"), Sibling.Ready))
        return false;
    if (!F.Check(TEXT("legitimate quoted/unicode native localized text imports through the same public path"),
            UP4AuthoringTools::ImportTableJson(Fixture.Table.Get(), Valid)))
        return false;
    const FText Value = Fixture.Text(TEXT("Retained"));
    const auto Ns = FTextInspector::GetNamespace(Value), K = FTextInspector::GetKey(Value);
    const auto Src = FTextInspector::GetSourceString(Value);
    F.Check(TEXT("engine-native localization metadata and every source code unit are exact"),
        Ns.IsSet() && TextNamespaceUtil::StripPackageNamespace(Ns.GetValue()) == Namespace && K.IsSet() &&
            K.GetValue() == Key && Src && *Src == Source && !Value.IsCultureInvariant() &&
            Sibling.Same(Fixture.Text(TEXT("Sibling"))));
    F.Verdict(TEXT("valid localized escape source has lossless public native readback"), Valid,
        UP4AuthoringTools::NativeTableReadback(Fixture.Table.Get(), Valid), true);
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeTextUnsupportedRuntimeHistory,
    "P4MapKit.Quality.NativeTextUnsupportedRuntimeHistory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeTextUnsupportedRuntimeHistory::RunTest(const FString&)
{
    using namespace P4NativeTextEnvelopeCoverage;
    FN138Suite F(*this, TEXT("NativeTextUnsupportedRuntimeHistory"));
    FFixture Fixture(F);
    if (!Fixture.Begin())
        return false;
    // Real public engine numeric history, never forced flags/private history.
    const FText Numeric = FText::AsNumber(int32(12345));
    Fixture.Property->SetPropertyValue_InContainer(Fixture.Table->GetRowMap().FindChecked(TEXT("Retained")), Numeric);
    Fixture.Table->GetOutermost()->SetDirtyFlag(false);
    FN138State Held(Fixture.Table.Get());
    const FTextState Text(Fixture.Text(TEXT("Retained"))), Sibling(Fixture.Text(TEXT("Sibling")));
    Held.Same(F);
    if (!F.Good)
        return false;
    if (!F.Check(TEXT("unsupported history and sibling have successful nonempty independent native serializations"),
            Text.Ready && Sibling.Ready))
        return false;
    auto Package = Fixture.Table->GetOutermost();
    const uint32 Flags = Package->GetPackageFlags();
    const FGuid Guid = Package->GetGuid(), Persistent = Package->GetPersistentGuid();
    const FName Filename = Package->FileName;
    const uint64 Size = Package->FileSize;
    FString Runtime;
    F.Check(TEXT("actual numeric FText history refuses lossy runtime transport"),
        !P4TextRuntimeJson(Fixture.Text(TEXT("Retained")), Runtime));
    F.Check(TEXT("public native rows adapter refuses unsupported actual history instead of display-string fallback"),
        UP4AuthoringTools::NativeTableRowsJson(Fixture.Table.Get()).IsEmpty());
    F.Verdict(TEXT("localized source does not masquerade as actual native numeric history"), Fixture.Initial,
        UP4AuthoringTools::NativeTableReadback(Fixture.Table.Get(), Fixture.Initial), false);
    Held.Same(F);
    F.Check(TEXT("unsupported-history reads preserve both native histories/serialized bytes/flags and sibling source"),
        Text.Same(Fixture.Text(TEXT("Retained"))) && Sibling.Same(Fixture.Text(TEXT("Sibling"))));
    F.Check(TEXT("unsupported-history reads preserve complete native package flags/identities/file state"),
        Package->GetPackageFlags() == Flags && Package->GetGuid() == Guid &&
            Package->GetPersistentGuid() == Persistent && Package->FileName == Filename && Package->FileSize == Size);
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

namespace P4RowsAfter222NativeMatrices
{
using namespace P4RowsCoverage138;
uint64 N222DoubleBits(double Value)
{
    uint64 Bits = 0;
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}
bool N222Refuse(FN138Suite& F, UObject* Object, const FString& Input)
{
    FN138State Held(Object, true);
    Held.Same(F);
    if (!F.Good)
        return false;
    FString Canonical(TEXT("must be cleared")), Error;
    const bool Accepted = P4PrepareExactPrivateProperties(Object, Input, Canonical, Error);
    F.Trace(
        TEXT("Actual native preparation refusal"), Input, FString(Accepted ? TEXT("true; ") : TEXT("false; ")) + Error);
    F.Check(TEXT("Malformed input explicitly refuses with diagnostic and no canonical publication"),
        !Accepted && !Error.IsEmpty() && Canonical.IsEmpty());
    Held.Same(F);
    return F.Good;
}
void N222Unselected(FN138Suite& F, const FN138State& Before, UObject* Object, FProperty* Selected)
{
    for (const auto& Copy : Before.PropertyCopies)
        if (Copy->Property != Selected)
            for (int32 Slot = 0; Slot < Copy->Property->ArrayDim; ++Slot)
                F.Check(TEXT("Every initialized native sibling/fixed slot retained on valid preparation"),
                    Copy->Same(Object, Slot));
    F.Check(TEXT("Private preparation keeps owner/flags/dirty state and emits no editor notification"),
        FindObject<UObject>(nullptr, *Before.Path) == Object &&
            Object->GetPathName().Equals(Before.Path, ESearchCase::CaseSensitive) &&
            Object->GetFlags() == Before.Flags && Object->GetOutermost()->IsDirty() == Before.Dirty &&
            Before.Pre == 0 && Before.Post == 0);
    const auto Metadata = Object->GetOutermost()->GetMetaData();
    F.Check(TEXT("Accepted private preparation retains exact sentinel metadata presence and code units"),
        Metadata->HasValue(Object, TEXT("P4Rows138Sentinel")) == Before.HadMetadata &&
            FString(Metadata->GetValue(Object, TEXT("P4Rows138Sentinel")))
                .Equals(Before.Metadata, ESearchCase::CaseSensitive));
}
bool N222MapExact(FMapProperty* Map, UObject* Object, FEnumProperty* Key, FDoubleProperty* Value, int64 First,
    uint64 FirstBits, int64 Second, uint64 SecondBits)
{
    FScriptMapHelper Actual(Map, Map->ContainerPtrToValuePtr<void>(Object));
    if (Actual.Num() != 2)
        return false;
    bool SawFirst = false, SawSecond = false;
    for (int32 Index = 0; Index < Actual.GetMaxIndex(); ++Index)
        if (Actual.IsValidIndex(Index))
        {
            const int64 Identity = Key->GetUnderlyingProperty()->GetSignedIntPropertyValue(Actual.GetKeyPtr(Index));
            const uint64 Bits = N222DoubleBits(Value->GetPropertyValue(Actual.GetValuePtr(Index)));
            if (Identity == First && Bits == FirstBits)
                SawFirst = true;
            else if (Identity == Second && Bits == SecondBits)
                SawSecond = true;
            else
                return false;
        }
    return SawFirst && SawSecond;
}
} //namespace P4RowsAfter222NativeMatrices

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RowsAfter222PrivateDoubleMap, "P4MapKit.Quality.RowsAfter222PrivateDoubleMap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RowsAfter222PrivateDoubleMap::RunTest(const FString&)
{
    using namespace P4RowsCoverage138;
    using namespace P4RowsAfter222NativeMatrices;
    FN138Suite F(*this, TEXT("After222PrivateDoubleMap"));
    auto Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CarrotSaveGame_Config"));
    auto Map = Class ? CastField<FMapProperty>(Class->FindPropertyByName(TEXT("ConfigDoubles"))) : nullptr;
    auto Key = Map ? CastField<FEnumProperty>(Map->KeyProp) : nullptr;
    auto Value = Map ? CastField<FDoubleProperty>(Map->ValueProp) : nullptr;
    if (!F.Check(TEXT("Actual recovered concrete enum/double map with serialized native fields"),
            Class && !Class->HasAnyClassFlags(CLASS_Abstract) && Key && Key->GetEnum() && Value &&
                Value->ElementSize == int32(sizeof(double)) && !Map->HasAnyPropertyFlags(CPF_Transient)))
        return false;
    const FString FirstName(TEXT("EConfigDoubleType::PlayTime")), SecondName(TEXT("EConfigDoubleType::MultiPlayTime"));
    const int64 First = Key->GetEnum()->GetValueByNameString(FirstName),
                Second = Key->GetEnum()->GetValueByNameString(SecondName);
    if (!F.Check(TEXT("Actual source enum key identities are distinct and supported"),
            First != INDEX_NONE && Second != INDEX_NONE && First != Second))
        return false;
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(GetTransientPackage(), Class, NAME_None, RF_Transient));
    if (!F.Check(TEXT("Disposable actual native double-map instance"), Object.IsValid()))
        return false;
    const EPropertyFlags Flags = Map->GetPropertyFlags();
    const auto Input = [&](const FString& Token)
    {
        return TEXT("{\"ConfigDoubles\":[[") + N138Quote(FirstName) + TEXT(",") + Token + TEXT("],[") +
            N138Quote(SecondName) + TEXT(",37.25]]}");
    };
    // The first finite double is outside every float's range. The second tests
    // actual sign bits in this import route, separately from text setters.
    for (int32 Case = 0; Case < 2; ++Case)
    {
        const FString Token = Case ? TEXT("-0.0") : TEXT("1e100");
        const uint64 Bits = Case ? uint64(0x8000000000000000ULL) : N222DoubleBits(1e100);
        FN138State Before(Object.Get(), true);
        Before.Same(F);
        if (!F.Good)
            return false;
        FString Canonical, Error;
        const FString Source = Input(Token);
        const bool Accepted = P4PrepareExactPrivateProperties(Object.Get(), Source, Canonical, Error);
        F.Trace(TEXT("Actual original double-map native preparation"), Source,
            FString(Accepted ? TEXT("true; ") : TEXT("false; ")) + Error);
        if (!F.Check(TEXT("Finite double width and sign are admitted by the actual native import route"), Accepted))
            return false;
        F.Check(TEXT("Independent native enum-key association and all double bits are exact"),
            N222MapExact(Map, Object.Get(), Key, Value, First, Bits, Second, N222DoubleBits(37.25)));
        auto Prepared = N138Parse(Canonical);
        const auto Pairs = Prepared.IsValid() ? Prepared->TryGetField(TEXT("ConfigDoubles")) : nullptr;
        FString RetainedToken;
        F.Check(TEXT("Canonical source pair retains numeric token instead of narrowing or rewriting it"),
            Pairs.IsValid() && Pairs->Type == EJson::Array && Pairs->AsArray().Num() == 2 &&
                Pairs->AsArray()[0]->Type == EJson::Array && Pairs->AsArray()[0]->AsArray().Num() == 2 &&
                Pairs->AsArray()[0]->AsArray()[1]->Type == EJson::Number &&
                Pairs->AsArray()[0]->AsArray()[1]->TryGetString(RetainedToken) &&
                RetainedToken.Equals(Token, ESearchCase::CaseSensitive));
        N222Unselected(F, Before, Object.Get(), Map);
    }
    for (const FString& Bad : {FString(TEXT("1e309")), FString(TEXT("-1e309")), FString(TEXT("\"1e100\"")),
             FString(TEXT("true")), FString(TEXT("null")), FString(TEXT("{}")), FString(TEXT("[]"))})
        if (!N222Refuse(F, Object.Get(), Input(Bad)))
            return false;
    F.Check(TEXT("Refusal retains exact signed zero and unrelated keyed double value"),
        N222MapExact(
            Map, Object.Get(), Key, Value, First, uint64(0x8000000000000000ULL), Second, N222DoubleBits(37.25)));
    {
        FN138State Before(Object.Get(), true);
        Before.Same(F);
        if (!F.Good)
            return false;
        const FString Empty(TEXT("{\"ConfigDoubles\":[]}"));
        FString Canonical, Error;
        F.Check(TEXT("Explicit native double-map deletion is supported without losing owner or siblings"),
            P4PrepareExactPrivateProperties(Object.Get(), Empty, Canonical, Error));
        F.Check(TEXT("Independent native double map is completely empty"),
            FScriptMapHelper(Map, Map->ContainerPtrToValuePtr<void>(Object.Get())).Num() == 0);
        N222Unselected(F, Before, Object.Get(), Map);
    }
    F.Check(TEXT("Original double-map reflection flags unchanged"), Map->GetPropertyFlags() == Flags);
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RowsAfter222ByteEnumAndNativeSet,
    "P4MapKit.Quality.RowsAfter222ByteEnumAndNativeSet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RowsAfter222ByteEnumAndNativeSet::RunTest(const FString&)
{
    using namespace P4RowsCoverage138;
    using namespace P4RowsAfter222NativeMatrices;
    FN138Suite F(*this, TEXT("After222ByteEnumAndNativeSet"));
    TStrongObjectPtr<USoundWave> Object(NewObject<USoundWave>(GetTransientPackage(), NAME_None, RF_Transient));
    auto Byte = CastField<FByteProperty>(Object->GetClass()->FindPropertyByName(TEXT("SoundGroup")));
    auto Set = CastField<FSetProperty>(Object->GetClass()->FindPropertyByName(TEXT("ConcurrencySet")));
    auto Element = Set ? CastField<FObjectProperty>(Set->ElementProp) : nullptr;
    if (!F.Check(TEXT("Installed genuine serialized byte-enum and native object set"),
            Byte && Byte->Enum && Byte->Enum->NumEnums() > 1 && Set && Element && Element->PropertyClass &&
                Element->PropertyClass->GetPathName() == TEXT("/Script/Engine.SoundConcurrency") &&
                !Element->PropertyClass->HasAnyClassFlags(CLASS_Abstract) &&
                !Byte->HasAnyPropertyFlags(CPF_Transient) && !Set->HasAnyPropertyFlags(CPF_Transient)))
        return false;
    TStrongObjectPtr<UObject> Concurrency(
        NewObject<UObject>(GetTransientPackage(), Element->PropertyClass, NAME_None, RF_Transient));
    UObject* const Reference = Concurrency.Get();
    FScriptSetHelper NativeSet(Set, Set->ContainerPtrToValuePtr<void>(Object.Get()));
    NativeSet.AddElement(&Reference);
    Object->Volume = 0.75f;
    const EPropertyFlags ByteFlags = Byte->GetPropertyFlags(), SetFlags = Set->GetPropertyFlags();
    const int32 ValidIndex = 1;
    const int64 ValidValue = Byte->Enum->GetValueByIndex(ValidIndex);
    if (!F.Check(TEXT("Native enum control has an actual representable byte value"),
            ValidValue >= 0 && ValidValue <= MAX_uint8))
        return false;
    const FString ValidName = Byte->Enum->GetNameStringByIndex(ValidIndex);
    {
        FN138State Before(Object.Get(), true);
        Before.Same(F);
        if (!F.Good)
            return false;
        const FString Input = TEXT("{\"SoundGroup\":") + N138Quote(ValidName) + TEXT("}");
        FString Canonical, Error;
        if (!F.Check(TEXT("Existing native import route accepts actual byte-enum name"),
                P4PrepareExactPrivateProperties(Object.Get(), Input, Canonical, Error)))
            return false;
        F.Check(TEXT("Actual byte enum value retained independently"),
            Byte->GetPropertyValue_InContainer(Object.Get()) == uint8(ValidValue));
        N222Unselected(F, Before, Object.Get(), Byte);
    }
    for (const FString& Bad : {FString(TEXT("true")), FString(TEXT("null")), FString(TEXT("1")), FString(TEXT("1.0")),
             FString(TEXT("[]")), FString(TEXT("{}")), FString(TEXT("\"P4MissingNativeSoundGroup\""))})
        if (!N222Refuse(F, Object.Get(), TEXT("{\"Volume\":1.25,\"SoundGroup\":") + Bad + TEXT("}")))
            return false;
    // A real initialized, populated set cannot be represented by the admitted
    // JSON transport. Both empty and populated JSON attempts must fail before
    // changing a valid earlier scalar; never silently drop existing native data.
    FN138State TargetBefore(Concurrency.Get(), true);
    TargetBefore.Same(F);
    if (!F.Good)
        return false;
    for (const FString& Bad : {FString(TEXT("[]")), FString(TEXT("{}")), FString(TEXT("null")),
             FString(TEXT("[")) + N138Quote(Concurrency->GetPathName()) + TEXT("]")})
    {
        if (!N222Refuse(F, Object.Get(), TEXT("{\"Volume\":1.25,\"ConcurrencySet\":") + Bad + TEXT("}")))
            return false;
        TargetBefore.Same(F);
        FScriptSetHelper Current(Set, Set->ContainerPtrToValuePtr<void>(Object.Get()));
        int32 Matches = 0;
        for (int32 Index = 0; Index < Current.GetMaxIndex(); ++Index)
            if (Current.IsValidIndex(Index))
                if (Element->GetObjectPropertyValue(Current.GetElementPtr(Index)) == Concurrency.Get())
                    ++Matches;
        F.Check(TEXT("The complete real native set and strong target reference remain intact"),
            Current.Num() == 1 && Matches == 1 && Concurrency.Get() == Reference && Object->Volume == 0.75f);
    }
    F.Check(TEXT("Original native enum/set metadata flags never change"),
        Byte->GetPropertyFlags() == ByteFlags && Set->GetPropertyFlags() == SetFlags);
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RowsAfter222NativeFieldAndPhysicalNul,
    "P4MapKit.Quality.RowsAfter222NativeFieldAndPhysicalNul",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RowsAfter222NativeFieldAndPhysicalNul::RunTest(const FString&)
{
    using namespace P4RowsCoverage138;
    using namespace P4RowsAfter222NativeMatrices;
    FN138Suite F(*this, TEXT("After222NativeFieldAndPhysicalNul"));
    auto Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CarrotSaveGame_Story"));
    auto Time = Class ? CastField<FDoubleProperty>(Class->FindPropertyByName(TEXT("PlayTime"))) : nullptr;
    auto Other = Class ? CastField<FDoubleProperty>(Class->FindPropertyByName(TEXT("MultiPlayTime"))) : nullptr;
    auto Path = Class ? CastField<FStrProperty>(Class->FindPropertyByName(TEXT("PathName"))) : nullptr;
    auto Async = Class ? CastField<FBoolProperty>(Class->FindPropertyByName(TEXT("bAsyncSuccess"))) : nullptr;
    if (!F.Check(
            TEXT("Actual recovered concrete save owner has serialized doubles and inherited transient string/bool"),
            Class && !Class->HasAnyClassFlags(CLASS_Abstract) && Time && Other && Path && Async &&
                Time->ArrayDim == 1 && !Time->HasAnyPropertyFlags(CPF_Transient) &&
                Path->HasAnyPropertyFlags(CPF_Transient) && Async->HasAnyPropertyFlags(CPF_Transient)))
        return false;
    TStrongObjectPtr<UObject> Object(NewObject<UObject>(GetTransientPackage(), Class, NAME_None, RF_Transient));
    Time->SetPropertyValue_InContainer(Object.Get(), 17.5);
    Other->SetPropertyValue_InContainer(Object.Get(), 37.25);
    Path->SetPropertyValue_InContainer(Object.Get(), TEXT("CaseExact/RetainedTransientPath"));
    Async->SetPropertyValue_InContainer(Object.Get(), true);
    const EPropertyFlags TimeFlags = Time->GetPropertyFlags(), PathFlags = Path->GetPropertyFlags(),
                         AsyncFlags = Async->GetPropertyFlags();
    const FString LongField = FString::ChrN(NAME_SIZE, TEXT('K'));
    for (const FString& Input : {FString(TEXT("{\"PlayTime\":23,\"PathName\":\"WouldReplaceTransient\"}")),
             FString(TEXT("{\"PlayTime\":23,\"bAsyncSuccess\":false}")), FString(TEXT("{\"PlayTime[0]\":23}")),
             FString(TEXT("{\"PlayTime[-1]\":23}")),
             FString(TEXT("{\"PlayTime\":23,")) + N138Quote(LongField) + TEXT(":1}"),
             FString(TEXT("{\"PlayTime\":23,\"MultiPlayTime\":true}"))})
        if (!N222Refuse(F, Object.Get(), Input))
            return false;
    FString Physical(TEXT("{\"PlayTime\":23,\"MultiPlayTime\":\"xTAIL\"}"));
    const int32 Index = Physical.Find(TEXT("xTAIL")), OriginalLength = Physical.Len();
    if (!F.Check(TEXT("Physical NUL input has an independently located genuine preimage"), Index >= 0))
        return false;
    Physical[Index] = TCHAR(0);
    if (!F.Check(TEXT("Owned input contains a physical NUL with length and every suffix code unit retained"),
            Physical.Len() == OriginalLength && Physical[Index] == TCHAR(0) &&
                Physical.Mid(Index + 1, 4).Equals(TEXT("TAIL"), ESearchCase::CaseSensitive)))
        return false;
    if (!N222Refuse(F, Object.Get(), Physical))
        return false;
    F.Check(TEXT("Exact initialized native scalar bits and transient values survive every refusal"),
        N222DoubleBits(Time->GetPropertyValue_InContainer(Object.Get())) == N222DoubleBits(17.5) &&
            N222DoubleBits(Other->GetPropertyValue_InContainer(Object.Get())) == N222DoubleBits(37.25) &&
            Path->GetPropertyValue_InContainer(Object.Get())
                .Equals(TEXT("CaseExact/RetainedTransientPath"), ESearchCase::CaseSensitive) &&
            Async->GetPropertyValue_InContainer(Object.Get()));
    F.Check(TEXT("Actual native serialized/transient flags remain unchanged"),
        Time->GetPropertyFlags() == TimeFlags && Path->GetPropertyFlags() == PathFlags &&
            Async->GetPropertyFlags() == AsyncFlags);
    F.NoDisk();
    return F.Good && !HasAnyErrors();
}

#endif
