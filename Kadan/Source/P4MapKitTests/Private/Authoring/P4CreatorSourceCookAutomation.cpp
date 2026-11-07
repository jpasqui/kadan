#include "P4CreatorSource.h"
#include "P4AuthoringRows.h"
#include "P4LevelContent.h"
#include "P4OriginalRecord.h"
#include "P4OriginalImportSave.h"
#include "P4PackageSave.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Animation/Skeleton.h"
#include "Animation/SmartName.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "IAssetTools.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
enum class ECookSourceFixture
{
    Material,
    Instance,
    Skeleton
};
// The same native skeleton initialization used by the existing skinned source fixture.
struct FCreatorCookSkeleton : USkeleton
{
    static void Initialize(USkeleton* Skeleton)
    {
        Skeleton->*(&FCreatorCookSkeleton::Guid) = FGuid::NewGuid();
        (Skeleton->*(&FCreatorCookSkeleton::BoneTree)).AddDefaulted();
        FReferenceSkeletonModifier Modifier(Skeleton->*(&FCreatorCookSkeleton::ReferenceSkeleton), Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
        // UE4.26 Skeleton.cpp:113 creates the empty AnimationCurves mapping, but
        // SmartName.cpp:299 cooks its loaded copy. Match Skeleton.cpp:165 before
        // the first fixture cook; the public container PostLoad copies it at 312.
        (Skeleton->*(&FCreatorCookSkeleton::SmartNames)).PostLoad();
    }
};
bool CookSourceFixture(FAutomationTestBase& Test, ECookSourceFixture Kind, const FString& Name)
{
    const FString Root = TEXT("/Game/__KadanCreatorSourceCookTest");
    const FString PackageName = Root + TEXT("/") + Name;
    const FString Path = PackageName + TEXT(".") + Name;
    const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
    if (FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName))
    {
        Test.AddError(TEXT("Cooked source fixture destination is occupied: ") + PackageName);
        return false;
    }
    ON_SCOPE_EXIT
    {
        if (UPackage* Package = FindPackage(nullptr, *PackageName))
        {
            Package->SetDirtyFlag(false);
            UPackageTools::UnloadPackages({Package});
        }
        for (const FString& Suffix :
            {FString(TEXT("uasset")), FString(TEXT("uexp")), FString(TEXT("ubulk")), FString(TEXT("uptnl"))})
        {
            const FString OwnedFile = FPaths::ChangeExtension(File, Suffix);
            FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*OwnedFile, false);
            if (IFileManager::Get().FileExists(*OwnedFile))
                IFileManager::Get().Delete(*OwnedFile, false, true);
        }
    };
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    UObject* Asset = nullptr;
    if (Kind == ECookSourceFixture::Material)
    {
        TStrongObjectPtr<UMaterialFactoryNew> Factory(NewObject<UMaterialFactoryNew>());
        Asset = Tools.CreateAsset(Name, Root, UMaterial::StaticClass(), Factory.Get());
    }
    else if (Kind == ECookSourceFixture::Instance)
    {
        TStrongObjectPtr<UMaterialInstanceConstantFactoryNew> Factory(NewObject<UMaterialInstanceConstantFactoryNew>());
        Factory->InitialParent = UMaterial::GetDefaultMaterial(MD_Surface);
        Asset = Tools.CreateAsset(Name, Root, UMaterialInstanceConstant::StaticClass(), Factory.Get());
    }
    else
    {
        USkeleton* Skeleton = NewObject<USkeleton>(
            CreatePackage(*PackageName), FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
        FCreatorCookSkeleton::Initialize(Skeleton);
        Asset = Skeleton;
        FAssetRegistryModule::AssetCreated(Asset);
    }
    if (!Test.TestNotNull(TEXT("Actual native comparison fixture exists"), Asset))
        return false;
    // A genuine preexisting null slot remains meaningful. The known intrinsic slot
    // must never justify dropping other nulls from the native array or cooked receipt.
    const FArrayProperty* UserDataArray = FindFProperty<FArrayProperty>(Asset->GetClass(), TEXT("AssetUserData"));
    const FObjectPropertyBase* UserDataObject = UserDataArray ? CastField<FObjectPropertyBase>(UserDataArray->Inner)
                                                              : nullptr;
    if (!Test.TestTrue(TEXT("Native fixture has a reflected object userdata array"), UserDataArray && UserDataObject))
        return false;
    FScriptArrayHelper UserData(UserDataArray, UserDataArray->ContainerPtrToValuePtr<void>(Asset));
    const FString WithoutNull = P4OriginalObjectAssetState(Asset);
    const int32 NullSlot = UserData.AddValue();
    Test.TestNull(TEXT("Preexisting native userdata slot is genuinely null"),
        UserDataObject->GetObjectPropertyValue(UserData.GetRawPtr(NullSlot)));
    // A synthetic source boundary, not evidence that these fixtures recover game appearance.
    TArray<FString> WarmLines;
    const FString BeforeRole = Kind == ECookSourceFixture::Skeleton
        ? P4OriginalObjectAssetStateAndLines(Asset, WarmLines)
        : P4OriginalObjectAssetState(Asset);
    Test.TestFalse(TEXT("A genuine preexisting null slot remains part of cooked semantic state"),
        WithoutNull.IsEmpty() || WithoutNull == BeforeRole);
    if (!Test.TestFalse(TEXT("Initial actual cooked semantic state is readable"), BeforeRole.IsEmpty()))
        return false;
    {
        P4ImportPublication::FImportScope Import;
        FP4OriginalRecord Original;
        Original.Package = PackageName;
        Original.Class = Asset->GetClass()->GetPathName();
        Original.Kind = Kind == ECookSourceFixture::Material ? TEXT("material")
            : Kind == ECookSourceFixture::Instance           ? TEXT("material-instance")
                                                             : TEXT("skeleton");
        FString Why;
        if (!Test.TestTrue(
                TEXT("Publish exact synthetic original identity: ") + Why, P4MarkOriginalAsset(Asset, Original, Why)))
            return false;
    }
    FString Origin;
    Test.TestTrue(
        TEXT("Actual original boundary establishes intrinsic role"), P4HasIntrinsicGameSource(Asset, &Origin));
    Test.TestEqual(TEXT("Intrinsic role retains original native package"), Origin, PackageName);
    IInterface_AssetUserData* Owner = CastChecked<IInterface_AssetUserData>(Asset);
    UAssetUserData* Role = Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass());
    const FString WithRole = P4OriginalObjectAssetState(Asset);
    Test.TestEqual(
        TEXT("Attaching editor-only origin preserves actual cooked game-semantic state"), WithRole, BeforeRole);
    Test.TestTrue(TEXT("Actual cook retains the live intrinsic array entry"),
        Owner->GetAssetUserDataOfClass(UP4CreatorSourceData::StaticClass()) == Role);
    UP4MaterialExportData* Other = NewObject<UP4MaterialExportData>(Asset);
    Owner->AddAssetUserData(Other);
    const FString WithOther = P4OriginalObjectAssetState(Asset);
    Test.TestFalse(TEXT("A different editor-only userdata entry still changes actual cooked state"),
        WithOther.IsEmpty() || WithOther == WithRole);
    Test.TestTrue(TEXT("Actual comparison leaves unrelated userdata intact"),
        Owner->GetAssetUserDataOfClass(UP4MaterialExportData::StaticClass()) == Other);
    // Exercise the proven marker at every position around one genuine null and one
    // different editor-only object. Reordering is test setup; comparisons never mutate it.
    for (int32 DesiredRoleSlot = 0; DesiredRoleSlot < UserData.Num(); ++DesiredRoleSlot)
    {
        int32 CurrentRoleSlot = INDEX_NONE;
        for (int32 Slot = 0; Slot < UserData.Num(); ++Slot)
            if (UserDataObject->GetObjectPropertyValue(UserData.GetRawPtr(Slot)) == Role)
                CurrentRoleSlot = Slot;
        if (!Test.TestTrue(
                TEXT("Known intrinsic object has a real native array position"), CurrentRoleSlot != INDEX_NONE))
            return false;
        UObject* Displaced = UserDataObject->GetObjectPropertyValue(UserData.GetRawPtr(DesiredRoleSlot));
        UserDataObject->SetObjectPropertyValue(UserData.GetRawPtr(CurrentRoleSlot), Displaced);
        UserDataObject->SetObjectPropertyValue(UserData.GetRawPtr(DesiredRoleSlot), Role);
        TArray<UObject*> BeforeArray;
        for (int32 Slot = 0; Slot < UserData.Num(); ++Slot)
            BeforeArray.Add(UserDataObject->GetObjectPropertyValue(UserData.GetRawPtr(Slot)));
        Test.TestEqual(TEXT("Exact intrinsic slot position leaves all other cooked nulls meaningful"),
            P4OriginalObjectAssetState(Asset), WithOther);
        Test.TestEqual(
            TEXT("Actual cook never changes the live userdata array count"), UserData.Num(), BeforeArray.Num());
        for (int32 Slot = 0; Slot < UserData.Num() && Slot < BeforeArray.Num(); ++Slot)
            Test.TestTrue(TEXT("Actual cook never changes the live userdata ordering or null positions"),
                UserDataObject->GetObjectPropertyValue(UserData.GetRawPtr(Slot)) == BeforeArray[Slot]);
    }
    Owner->RemoveUserDataOfClass(UP4MaterialExportData::StaticClass());
    Test.TestEqual(
        TEXT("Removing only unrelated fixture data restores state"), P4OriginalObjectAssetState(Asset), WithRole);
    if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        const bool Previous = Material->TwoSided;
        Material->TwoSided = !Previous;
        const FString Changed = P4OriginalObjectAssetState(Asset);
        Test.TestFalse(
            TEXT("A runtime material setting still changes cooked state"), Changed.IsEmpty() || Changed == WithRole);
        Material->TwoSided = Previous;
    }
    else if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset))
    {
        const FMaterialInstanceBasePropertyOverrides Previous = Instance->BasePropertyOverrides;
        Instance->BasePropertyOverrides.bOverride_TwoSided = true;
        Instance->BasePropertyOverrides.TwoSided = true;
        const FString Changed = P4OriginalObjectAssetState(Asset);
        Test.TestFalse(TEXT("A runtime material instance override still changes cooked state"),
            Changed.IsEmpty() || Changed == WithRole);
        Instance->BasePropertyOverrides = Previous;
    }
    else
    {
        USkeleton* Skeleton = CastChecked<USkeleton>(Asset);
        const FName RetargetName(TEXT("SourceRegression"));
        FReferencePose Pose;
        Pose.PoseName = RetargetName;
        Pose.ReferencePose.Add(FTransform(FVector(10, 0, 0)));
        Skeleton->AnimRetargetSources.Add(RetargetName, Pose);
        const FString Changed = P4OriginalObjectAssetState(Asset);
        Test.TestFalse(TEXT("A native skeleton retarget pose still changes cooked state"),
            Changed.IsEmpty() || Changed == WithRole);
        Skeleton->AnimRetargetSources.Remove(RetargetName);
    }
    Test.TestEqual(
        TEXT("Restoring the runtime field restores actual cooked state"), P4OriginalObjectAssetState(Asset), WithRole);
    FString Why;
    const bool Saved = P4SaveAssetPackage(Asset, [&] { return P4OriginalObjectAssetState(Asset) == BeforeRole; }, Why);
    if (!Test.TestTrue(TEXT("Actual original package save preserves its pre-role semantic baseline: ") + Why, Saved))
        return false;
    UPackage* Package = Asset->GetOutermost();
    UClass* Class = Asset->GetClass();
    Asset = nullptr;
    FText UnloadError;
    if (!Test.TestTrue(
            TEXT("Actual original fixture package unloads"), UPackageTools::UnloadPackages({Package}, UnloadError)))
        return false;
    if (!Test.TestNull(TEXT("Previous original fixture object is released"), FindObject<UObject>(nullptr, *Path)))
        return false;
    Asset = StaticLoadObject(Class, nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!Test.TestNotNull(TEXT("Original fixture is reopened from its actual package"), Asset))
        return false;
    Test.TestTrue(TEXT("Cold original retains exact intrinsic source role"),
        P4HasIntrinsicGameSource(Asset, &Origin) && Origin == PackageName);
    TArray<FString> ColdLines;
    const FString ColdState = Kind == ECookSourceFixture::Skeleton
        ? P4OriginalObjectAssetStateAndLines(Asset, ColdLines)
        : P4OriginalObjectAssetState(Asset);
    if (ColdState != BeforeRole && Kind == ECookSourceFixture::Skeleton)
    {
        int32 Removed = 0, Added = 0;
        TArray<FString> FirstRemoved, FirstAdded;
        for (const FString& Line : WarmLines)
            if (!ColdLines.Contains(Line))
            {
                ++Removed;
                if (FirstRemoved.Num() < 2)
                    FirstRemoved.Add(Line);
            }
        for (const FString& Line : ColdLines)
            if (!WarmLines.Contains(Line))
            {
                ++Added;
                if (FirstAdded.Num() < 2)
                    FirstAdded.Add(Line);
            }
        Test.AddInfo(
            FString::Printf(TEXT("Skeleton cold semantic drift: warm-lines=%d cold-lines=%d removed=%d added=%d"),
                WarmLines.Num(), ColdLines.Num(), Removed, Added));
        for (const FString& Line : FirstRemoved)
            Test.AddInfo(TEXT("Skeleton cold removed: ") + Line);
        for (const FString& Line : FirstAdded)
            Test.AddInfo(TEXT("Skeleton cold added: ") + Line);
    }
    Test.TestEqual(TEXT("Cold actual cooked state still matches the pre-role baseline"), ColdState, BeforeRole);
    return !Test.HasAnyErrors();
}
} //namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorCookMaterial, "P4MapKit.Kadan.CreatorSource.CookedMaterial",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorCookMaterial::RunTest(const FString&)
{
    return CookSourceFixture(*this, ECookSourceFixture::Material, TEXT("Material"));
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorCookInstance, "P4MapKit.Kadan.CreatorSource.CookedMaterialInstance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorCookInstance::RunTest(const FString&)
{
    return CookSourceFixture(*this, ECookSourceFixture::Instance, TEXT("Instance"));
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorCookSkeleton, "P4MapKit.Kadan.CreatorSource.CookedSkeleton",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorCookSkeleton::RunTest(const FString&)
{
    return CookSourceFixture(*this, ECookSourceFixture::Skeleton, TEXT("Skeleton"));
}
#endif
