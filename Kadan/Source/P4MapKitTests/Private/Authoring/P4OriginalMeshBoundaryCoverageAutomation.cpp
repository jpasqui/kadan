// Private mesh-only regression draft. Not compiled or executed by this worker.
// Mutations are bounded owned copies of the exact original Cylinder pair.
#include "Policies/CondensedJsonPrintPolicy.h"
#include "P4AuthoringRows.h"
#include "P4OriginalMeshStaging.h"
#include "P4OriginalMeshNegativeTests.h"
#include "P4NativeHash.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "Engine/PointLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "UObject/MetaData.h"

#include "P4OriginalMeshCoverageShared.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Mesh89UnusedBulkFiles, "P4MapKit.Quality.Mesh89UnusedBulkFiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Mesh89UnusedBulkFiles::RunTest(const FString&)
{
    using namespace P4MeshCoverage89;
    FM89Suite F(*this, TEXT("UnusedSidecars"));
    if (!F.Open())
        return false;
    for (const int32 Mask : {1, 2, 4, 7})
    {
        FM89Case C;
        if (!F.Capture(F.Header, F.Body, C, Mask))
            return false;
        FP4OriginalMeshSource Out;
        FString Error;
        const bool Accepted = P4DecodeOriginalMesh(C.Header, FString(), M89Export, Out, Error);
        F.Trace(FString::Printf(TEXT("actual-unused-bulk-files-%d"), Mask), Accepted, Error);
        F.Check(TEXT("Bulk files the mesh does not refer to are read and change nothing"),
            Accepted && Out.MaterialsValidated && Out.RenderDecoded && C.Paths.Num() > 2 && C.Digest == M89Digest);
        F.Check(TEXT("No attachment invents absent source attributes or changes native geometry"),
            M89GeometryBits(Out, F.Control) && Out.Materials.Num() == 1 &&
                Out.Materials[0].Slot.ObjectIndex == F.Control.Materials[0].Slot.ObjectIndex);
        const auto J = M89Parse(P4StageOriginalMesh(C.Header, FString(), M89Export));
        bool Value = true;
        if (!F.Check(TEXT("Staging report exists"), J.IsValid()))
            return false;
        // The cylinder has no color buffer, which in a cooked mesh means every color is opaque white.
        F.Check(TEXT("The mesh stages whatever unused bulk files lie beside it"),
            J->TryGetBoolField(TEXT("valid"), Value) && Value && J->GetBoolField(TEXT("materials_validated")) &&
                J->GetBoolField(TEXT("render_payload_decoded")) &&
                J->GetBoolField(TEXT("geometry_attributes_verified")) &&
                J->GetBoolField(TEXT("private_staging_qualified")));
        for (const TCHAR* Flag : {TEXT("editable_import_qualified"), TEXT("editable_asset_qualified"),
                 TEXT("original_defaults_qualified"), TEXT("dependencies_qualified"), TEXT("full_asset_qualified")})
            F.Check(FString(TEXT("Staging alone does not qualify an asset: ")) + Flag,
                J->TryGetBoolField(Flag, Value) && !Value);
        // SHA-256 of 52 vertices of FF FF FF FF.
        F.Check(TEXT("One LOD of 52 vertices"), J->GetArrayField(TEXT("lods")).Num() == 1);
        for (const auto& LOD : J->GetArrayField(TEXT("lods")))
            F.Check(TEXT("A mesh without a color buffer reads back opaque white at every source vertex"),
                !LOD->AsObject()->GetBoolField(TEXT("color_present")) &&
                    LOD->AsObject()->GetIntegerField(TEXT("vertices")) == 52 &&
                    LOD->AsObject()->GetStringField(TEXT("colors_sha256")) ==
                        TEXT("86ff85227f7cc42cc3293ebdeeb10d8660b51eb429dd1d6c999ddf14cb59db73"));
        C.Same(F);
    }
    F.Final();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Mesh89BoundSourceCorruptions, "P4MapKit.Quality.Mesh89BoundSourceCorruptions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Mesh89BoundSourceCorruptions::RunTest(const FString&)
{
    using namespace P4MeshCoverage89;
    FM89Suite F(*this, TEXT("BoundSourceCorruptions"));
    if (!F.Open())
        return false;
    TArray<uint8> BadHeader = F.Header;
    if (!M89Write(F, BadHeader, 0, 0x9e2a83c1u, 0x9e2a83c0u))
        return false;
    FM89Case Magic;
    if (!F.Capture(BadHeader, F.Body, Magic))
        return false;
    F.Refuses(TEXT("authentic-header-magic-corruption"), Magic, TEXT(""));
    TArray<uint8> BadTag = F.Body;
    if (!M89Write(F, BadTag, 3905, 74, 0xffffffffu))
        return false;
    FM89Case Tag;
    if (!F.Capture(F.Header, BadTag, Tag))
        return false;
    F.Refuses(TEXT("actual-first-static-mesh-tag-name-index-outside-source-table"), Tag, TEXT(""));
    // Exact original name71 is SM_stg_Cylinder01, absent from all mesh tags.
    // Name47 collides with a later genuine field and tests a different refusal.
    TArray<uint8> Missing = F.Body;
    if (!M89Write(F, Missing, 3905, 74, 71))
        return false;
    FM89Case Materials;
    if (!F.Capture(F.Header, Missing, Materials))
        return false;
    F.Refuses(TEXT("actual-static-materials-field-renamed-without-invented-default"), Materials,
        TEXT("original-mesh-static-materials-source-missing-or-schema"));
    F.Final();
    return F.Good && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Mesh89ExplicitNullAndIndexedTag, "P4MapKit.Quality.Mesh89ExplicitNullAndIndexedTag",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Mesh89ExplicitNullAndIndexedTag::RunTest(const FString&)
{
    using namespace P4MeshCoverage89;
    FM89Suite F(*this, TEXT("ExplicitNullAndIndexedTag"));
    if (!F.Open())
        return false;
    // Independent ue_tagged/native-footer census finds exactly two -5 material
    // reference words: tagged4016 and native7603. Both must become explicit None.
    TArray<uint8> NullBody = F.Body;
    if (!M89Write(F, NullBody, 4016, 0xfffffffbu, 0) || !M89Write(F, NullBody, 7603, 0xfffffffbu, 0))
        return false;
    FM89Case Null;
    if (!F.Capture(F.Header, NullBody, Null))
        return false;
    FP4OriginalMeshSource Out;
    FString Error;
    const bool Accepted = P4DecodeOriginalMesh(Null.Header, FString(), M89Export, Out, Error);
    F.Trace(TEXT("matching-tagged-and-native-explicit-null-reference"), Accepted, Error);
    if (!F.Check(TEXT("Actual explicit None differs from an omitted material field"),
            Accepted && Out.MaterialsValidated && Out.Materials.Num() == 1 && Out.Materials[0].Slot.ObjectIndex == 0 &&
                Out.Materials[0].ObjectPath.IsEmpty() && Out.Render.Materials.size() == 1 &&
                Out.Render.Materials[0].ObjectIndex == 0))
        return false;
    F.Check(TEXT("Explicit null changes no source geometry or retained native names/UV bits"),
        M89GeometryBits(Out, F.Control) &&
            Out.Materials[0].NativeName.Display == F.Control.Materials[0].NativeName.Display &&
            Out.Materials[0].ImportedName.Display == F.Control.Materials[0].ImportedName.Display &&
            FMemory::Memcmp(Out.Materials[0].UVDensities.data(), F.Control.Materials[0].UVDensities.data(), 16) == 0);
    Null.Same(F);
    TArray<uint8> IndexedBody = F.Body;
    if (!M89Write(F, IndexedBody, 4339, 0, 1))
        return false;
    FM89Case Indexed;
    if (!F.Capture(F.Header, IndexedBody, Indexed))
        return false;
    FP4OriginalMeshSource IndexedOut;
    const bool IndexedAccepted = P4DecodeOriginalMesh(Indexed.Header, FString(), M89Export, IndexedOut, Error);
    F.Trace(TEXT("actual-indexed-unapplied-native-tag-retained"), IndexedAccepted, Error);
    F.Check(TEXT("Indexed original tag is preserved with its actual slot identity"),
        IndexedAccepted && IndexedOut.OtherTaggedProperties.Contains(TEXT("LightmapUVDensity[1]")) &&
            !IndexedOut.OtherTaggedProperties.Contains(TEXT("LightmapUVDensity")) &&
            IndexedOut.OtherTaggedProperties.Num() == F.Control.OtherTaggedProperties.Num() &&
            M89GeometryBits(IndexedOut, F.Control));
    const auto J = M89Parse(P4StageOriginalMesh(Indexed.Header, FString(), M89Export));
    bool Mentioned = false;
    if (!F.Check(TEXT("Indexed source has a retained staging gap report"), J.IsValid()))
        return false;
    for (const auto& Gap : J->GetArrayField(TEXT("unrepresented_fields")))
        if (Gap->AsString() == TEXT("Unapplied original tagged property: LightmapUVDensity[1]"))
            Mentioned = true;
    F.Check(TEXT("Unapplied indexed tag is reported rather than silently stripped"),
        Mentioned && !J->GetBoolField(TEXT("full_asset_qualified")));
    Indexed.Same(F);
    F.Final();
    return F.Good && !HasAnyErrors();
}

// The engine's own mesh builder, given the game's cylinder, makes the game's triangles: every corner's
// position, packed tangent and normal, UVs and color, in the game's section with its flags, with the
// game's bounds and LOD screen size. The game's file holds 48 triangles on 52 vertices.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalMeshBuiltEqualsGame, "P4MapKit.Quality.OriginalMeshBuiltEqualsGame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalMeshBuiltEqualsGame::RunTest(const FString&)
{
    using namespace P4MeshCoverage89;
    FM89Suite F(*this, TEXT("BuiltEqualsGame"));
    if (!F.Open())
        return false;
    const auto J = M89Parse(UP4AuthoringTools::NativeBuildOriginalMesh(F.ControlCase.Header, FString(), M89Export));
    bool Value = false;
    if (!F.Check(TEXT("The engine builds the game's cylinder"),
            J.IsValid() && J->TryGetBoolField(TEXT("valid"), Value) && Value))
    {
        if (J.IsValid())
            AddError(J->GetStringField(TEXT("error")));
        return false;
    }
    const auto& LODs = J->GetArrayField(TEXT("lods"));
    if (!F.Check(TEXT("One LOD"), LODs.Num() == 1))
        return false;
    const auto LOD = LODs[0]->AsObject();
    F.Check(TEXT("48 triangles on 52 vertices in the game's file, and 52 vertices built"),
        LOD->GetIntegerField(TEXT("triangles")) == 48 && LOD->GetIntegerField(TEXT("source_vertices")) == 52 &&
            LOD->GetIntegerField(TEXT("built_vertices")) == 52);
    for (const TCHAR* Field : {TEXT("sections_equal"), TEXT("layout_equal"), TEXT("triangles_equal")})
        F.Check(
            FString(TEXT("The built LOD equals the game's: ")) + Field, LOD->TryGetBoolField(Field, Value) && Value);
    for (const TCHAR* Field : {TEXT("bounds_equal"), TEXT("screen_sizes_equal"), TEXT("equal")})
        F.Check(FString(TEXT("The built mesh equals the game's: ")) + Field, J->TryGetBoolField(Field, Value) && Value);
    const auto& Missing = J->GetArrayField(TEXT("missing_materials"));
    F.Check(TEXT("The game's material is named, and is not in this project"),
        Missing.Num() == 1 &&
            Missing[0]->AsString() ==
                TEXT("/Game/Carrot4/MasterMaterial/Collision/MI_Collision_Wireframe.MI_Collision_Wireframe"));
    // The mesh was built to be compared and is discarded: after a collection no static mesh object stays.
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    F.Final();
    return F.Good && !HasAnyErrors();
}
#endif

// The game's cylinder kept in Content: refused while its material is not in the project, then made at
// its game path with its material slot, lightmap and collision settings and a record of what it was
// imported as, saved with the material, read back from the saved file, left alone by a second import,
// and known as edited once a setting or a vertex changes.
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4StaticMeshContent.h"
#include "P4CollisionSettings.h"
#include "P4MaterialGraph.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalMeshKeptTest, "P4MapKit.Quality.OriginalMeshKeptInContent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalMeshKeptTest::RunTest(const FString&)
{
    using namespace P4MeshCoverage89;
    FM89Suite F(*this, TEXT("KeptInContent"));
    if (!F.Open())
        return false;
    const FString MeshPackage = TEXT("/Game/Carrot4/Objects/Otakara/OtakaraCollision/SM_stg_Cylinder01");
    const FString MaterialPackage = TEXT("/Game/Carrot4/MasterMaterial/Collision/MI_Collision_Wireframe");
    const FString Reload = TEXT("/Temp/P4OriginalMeshKeptReload");
    TArray<FString> Files;
    for (const FString& Package : {MeshPackage, MaterialPackage})
    {
        FString File;
        FPackageName::TryConvertLongPackageNameToFilename(Package, File, TEXT(".uasset"));
        Files.Add(FPaths::ConvertRelativePathToFull(File));
        if (!F.Check(TEXT("Nothing is at the test's places before it"),
                !FPaths::FileExists(Files.Last()) && FindPackage(nullptr, *Package) == nullptr))
            return false;
    }
    // Everything the test made goes, before the suite's own closing checks and on every way out.
    bool Cleaned = false;
    const auto Cleanup = [&]()
    {
        if (Cleaned)
            return;
        Cleaned = true;
        for (const FString& Package : {MeshPackage, Reload, MaterialPackage})
            if (UPackage* Made = FindPackage(nullptr, *Package))
            {
                TArray<UObject*> Inside;
                GetObjectsWithOuter(Made, Inside, false);
                for (UObject* Object : Inside)
                    if (Object->IsAsset())
                    {
                        if (!Package.StartsWith(TEXT("/Temp/")))
                            FAssetRegistryModule::AssetDeleted(Object);
                        Object->ClearFlags(RF_Public | RF_Standalone);
                        Object->MarkPendingKill();
                    }
                ResetLoaders(Made);
                Made->SetDirtyFlag(false);
            }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        for (const FString& File : Files)
        {
            IFileManager::Get().Delete(*File, false, true, true);
            for (FString Folder = FPaths::GetPath(File);
                FPaths::IsUnderDirectory(Folder, Content) && !FPaths::IsSamePath(Folder, Content);
                Folder = FPaths::GetPath(Folder))
                if (!IFileManager::Get().DeleteDirectory(*Folder, false, false))
                    break;
        }
    };
    ON_SCOPE_EXIT
    {
        Cleanup();
    };
    const auto Import = [&](bool Save)
    { return M89Parse(UP4AuthoringTools::NativeImportOriginalMesh(F.ControlCase.Header, FString(), Save)); };
    const auto Flag = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        bool Value = false;
        return J.IsValid() && J->TryGetBoolField(Name, Value) && Value;
    };
    const auto Word = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        FString Value;
        if (J.IsValid())
            J->TryGetStringField(Name, Value);
        return Value;
    };

    // Collision settings the project would rewrite when they load. The test project has the game's
    // collision profiles and channels, as a project the game's maps are imported into has to.
    {
        const FStructProperty* Responses = FindFProperty<FStructProperty>(
            FBodyInstance::StaticStruct(), TEXT("CollisionResponses"));
        const FArrayProperty* Rows = FindFProperty<FArrayProperty>(
            FCollisionResponse::StaticStruct(), TEXT("ResponseArray"));
        const FNameProperty* Profile = FindFProperty<FNameProperty>(
            FBodyInstance::StaticStruct(), TEXT("CollisionProfileName"));
        const FByteProperty* Type = FindFProperty<FByteProperty>(FBodyInstance::StaticStruct(), TEXT("ObjectType"));
        FCollisionResponseTemplate Terrain;
        if (F.Check(TEXT("The engine's collision settings are where the test looks"),
                Responses && Rows && Profile && Type) &&
            F.Check(TEXT("The test project has the game's collision setup"),
                UCollisionProfile::Get()->GetProfileTemplate(FName(TEXT("CP_Terrain")), Terrain)))
        {
            // A body's settings as a file would give them: set as properties, without the engine's setters.
            const auto Body = [&](const TCHAR* ProfileName, ECollisionChannel Kind, const TCHAR* Channel)
            {
                UBodySetup* Made = NewObject<UBodySetup>(GetTransientPackage(), NAME_None, RF_Transient);
                Profile->SetPropertyValue_InContainer(&Made->DefaultInstance, FName(ProfileName));
                Type->SetPropertyValue_InContainer(&Made->DefaultInstance, uint8(Kind));
                if (Channel)
                {
                    FScriptArrayHelper Array(Rows,
                        Rows->ContainerPtrToValuePtr<void>(
                            Responses->ContainerPtrToValuePtr<void>(&Made->DefaultInstance)));
                    FResponseChannel* Row = reinterpret_cast<FResponseChannel*>(Array.GetRawPtr(Array.AddValue()));
                    Row->Channel = FName(Channel);
                    Row->Response = ECR_Ignore;
                }
                return Made;
            };
            UBodySetup* Own = Body(TEXT("Custom"), ECC_PhysicsBody, TEXT("Pawn"));
            F.Check(TEXT("Settings without a profile, on a channel of the project, come through"),
                P4CollisionProjectIssue(Own->DefaultInstance, Own).IsEmpty() &&
                    Own->DefaultInstance.GetResponseToChannel(ECC_Pawn) == ECR_Ignore);
            UBodySetup* NoProfile = Body(TEXT("CP_NoSuchProfile"), ECC_WorldStatic, nullptr);
            F.Check(TEXT("A profile the project does not have is named"),
                P4CollisionProjectIssue(NoProfile->DefaultInstance, NoProfile) ==
                    TEXT("collision-profile-not-in-the-project: CP_NoSuchProfile"));
            F.Check(TEXT("Naming it changed nothing"),
                NoProfile->DefaultInstance.GetCollisionProfileName() == FName(TEXT("CP_NoSuchProfile")));
            UBodySetup* NoChannel = Body(TEXT("Custom"), ECC_WorldStatic, TEXT("CH_NoSuchChannel"));
            F.Check(TEXT("A channel the project does not have is named"),
                P4CollisionProjectIssue(NoChannel->DefaultInstance, NoChannel) ==
                    TEXT("collision-channel-not-in-the-project: CH_NoSuchChannel"));
            // The game's terrain profile, in a file that says none of the responses the profile has.
            UBodySetup* Differs = Body(TEXT("CP_Terrain"), ECC_WorldStatic, nullptr);
            F.Check(TEXT("A profile the project defines differently is named"),
                P4CollisionProjectIssue(Differs->DefaultInstance, Differs) ==
                    TEXT("collision-profile-differs-in-the-project: CP_Terrain"));
            F.Check(TEXT("What a load made of them comes through a second load"),
                P4CollisionProjectIssue(Differs->DefaultInstance, Differs).IsEmpty() &&
                    Differs->DefaultInstance.GetCollisionProfileName() == FName(TEXT("CP_Terrain")) &&
                    Differs->DefaultInstance.GetResponseToChannel(ECC_Vehicle) ==
                        Terrain.ResponseToChannels.GetResponse(ECC_Vehicle) &&
                    Terrain.ResponseToChannels.GetResponse(ECC_Vehicle) == ECR_Ignore);
            for (UBodySetup* Made : {Own, NoProfile, NoChannel, Differs})
                Made->MarkPendingKill();
        }
    }

    const auto Without = Import(false);
    F.Check(TEXT("Without its material in the project the mesh is refused"),
        !Flag(Without, TEXT("valid")) &&
            Word(Without, TEXT("error")) ==
                TEXT(
                    "mesh-import-material-not-in-the-project: /Game/Carrot4/MasterMaterial/Collision/MI_Collision_Wireframe.MI_Collision_Wireframe"));
    F.Check(TEXT("A refused mesh leaves nothing"), FindObject<UStaticMesh>(nullptr, M89Export) == nullptr);

    FString Error;
    UMaterial* Material = P4BuildMaterialFromGraph(
        TEXT(
            "{\"nodes\": {\"Constant_0\": {\"type\": \"Constant\", \"inputs\": {}, \"props\": {\"R\": 0.5}}}, \"outputs\": {\"BaseColor\": [\"Constant_0\", \"\"]}}"),
        MaterialPackage, Error);
    if (!F.Check(TEXT("A material is made at the path the mesh names"), Material != nullptr))
    {
        AddError(Error);
        return false;
    }

    const auto First = Import(true);
    if (!F.Check(TEXT("The mesh is imported"), Flag(First, TEXT("valid"))))
    {
        AddError(Word(First, TEXT("error")));
        return false;
    }
    F.Check(TEXT("It was made and saved"), Flag(First, TEXT("created")) && Flag(First, TEXT("saved")));
    F.Check(TEXT("It equals the game's render data exactly"), Word(First, TEXT("equal")) == TEXT("exact"));
    const FString State = Word(First, TEXT("state"));
    F.Check(TEXT("Its state is a digest, and is what was recorded"),
        State.Len() == 64 && Word(First, TEXT("imported_state")) == State);
    UStaticMesh* Mesh = FindObject<UStaticMesh>(nullptr, M89Export);
    if (!F.Check(TEXT("An ordinary static mesh is at the game's path"), Mesh != nullptr))
        return false;
    F.Check(TEXT("It is an asset others can refer to"),
        Mesh->IsAsset() && Mesh->HasAllFlags(RF_Public | RF_Standalone) && !Mesh->HasAnyFlags(RF_Transient));
    F.Check(TEXT("One LOD of 48 triangles on 52 vertices"),
        Mesh->RenderData && Mesh->RenderData->LODResources.Num() == 1 &&
            Mesh->RenderData->LODResources[0].GetNumTriangles() == 48 &&
            Mesh->RenderData->LODResources[0].GetNumVertices() == 52);
    F.Check(TEXT("Its one slot holds the material"),
        Mesh->StaticMaterials.Num() == 1 && Mesh->StaticMaterials[0].MaterialInterface == Material);
    F.Check(TEXT("Its source geometry is editable"),
        Mesh->GetNumSourceModels() == 1 && Mesh->GetMeshDescription(0) != nullptr);
    F.Check(TEXT("It has collision when the game's has, and none when it has none"),
        (Mesh->BodySetup != nullptr) == F.Control.HasBodySetup);
    F.Check(TEXT("Both files are there and the package is not left dirty"),
        FPaths::FileExists(Files[0]) && FPaths::FileExists(Files[1]) && !Mesh->GetOutermost()->IsDirty());
    F.Check(TEXT("Its state after the save"), P4StaticMeshState(Mesh) == State);
    const auto Status = M89Parse(UP4AuthoringTools::NativeOriginalImportDiagnostic(Mesh));
    F.Check(TEXT("It is known as imported from the game, and as untouched"),
        Flag(Status, TEXT("valid")) && Flag(Status, TEXT("original")) && Flag(Status, TEXT("unchanged")));

    UPackage* Second = LoadPackage(CreatePackage(*Reload), *Files[0], LOAD_NoWarn);
    UStaticMesh* Saved = Second ? FindObject<UStaticMesh>(Second, TEXT("SM_stg_Cylinder01")) : nullptr;
    if (F.Check(TEXT("The saved file loads"), Saved != nullptr && Saved != Mesh))
        F.Check(TEXT("The saved mesh has the same state"), P4StaticMeshState(Saved) == State);

    const auto Again = Import(true);
    F.Check(TEXT("A second import of the same file changes nothing"),
        Flag(Again, TEXT("valid")) && !Flag(Again, TEXT("created")) && !Flag(Again, TEXT("saved")) &&
            FindObject<UStaticMesh>(nullptr, M89Export) == Mesh);

    const int32 Resolution = Mesh->LightMapResolution;
    Mesh->LightMapResolution = Resolution + 16;
    Mesh->MarkPackageDirty();
    F.Check(TEXT("A changed setting is seen"),
        !Flag(M89Parse(UP4AuthoringTools::NativeOriginalImportDiagnostic(Mesh)), TEXT("unchanged")));
    const auto Refused = Import(true);
    F.Check(TEXT("An edited mesh is not imported over"),
        !Flag(Refused, TEXT("valid")) &&
            Word(Refused, TEXT("error")) == TEXT("mesh-import-would-replace-an-edited-mesh"));
    Mesh->LightMapResolution = Resolution;
    F.Check(TEXT("The setting put back makes it untouched again"),
        Flag(M89Parse(UP4AuthoringTools::NativeOriginalImportDiagnostic(Mesh)), TEXT("unchanged")));
    if (FMeshDescription* Description = Mesh->GetMeshDescription(0))
    {
        FStaticMeshAttributes Attributes(*Description);
        for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
        {
            Attributes.GetVertexPositions()[Vertex] += FVector(1.0f, 0.0f, 0.0f);
            break;
        }
        F.Check(TEXT("A moved vertex is seen"),
            !Flag(M89Parse(UP4AuthoringTools::NativeOriginalImportDiagnostic(Mesh)), TEXT("unchanged")));
    }
    Cleanup();
    F.Final();
    return F.Good && !HasAnyErrors();
}

#include "P4LevelContent.h"
#include "P4LevelExport.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/Brush.h"
#include "GameFramework/WorldSettings.h"

// One of the game's levels kept in Content: Cave000's sound level, which holds its world settings and a
// level script with one variable. The file is the user's own, found below the native work folder, and
// the test project has the game's world settings class as its own.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalLevelKeptTest, "P4MapKit.Quality.OriginalLevelKeptInContent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalLevelKeptTest::RunTest(const FString&)
{
    const FString Package = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Sublevels/Cave000_F00_Sound");
    const FString Asset = Package + TEXT(".Cave000_F00_Sound");
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT")) /
        TEXT(
            "host/Saved/P4MapKit/FullNativeCave000/game/Carrot4/Content/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Sublevels/Cave000_F00_Sound.umap");
    FString File;
    FPackageName::TryConvertLongPackageNameToFilename(Package, File, TEXT(".umap"));
    File = FPaths::ConvertRelativePathToFull(File);
    if (!TestTrue(TEXT("the game's level file is where the test looks"), IFileManager::Get().FileExists(*Header)) ||
        !TestTrue(TEXT("nothing is at the test's place before it"),
            !IFileManager::Get().FileExists(*File) && FindPackage(nullptr, *Package) == nullptr))
        return false;
    bool Cleaned = false;
    const auto Cleanup = [&]()
    {
        if (Cleaned)
            return;
        Cleaned = true;
        if (UPackage* Made = FindPackage(nullptr, *Package))
        {
            if (UWorld* World = UWorld::FindWorldInPackage(Made))
            {
                FAssetRegistryModule::AssetDeleted(World);
                World->ClearFlags(RF_Public | RF_Standalone);
                World->DestroyWorld(false);
                World->MarkPendingKill();
            }
            ResetLoaders(Made);
            Made->SetDirtyFlag(false);
        }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        IFileManager::Get().Delete(*File, false, true, true);
        for (FString Folder = FPaths::GetPath(File);
            FPaths::IsUnderDirectory(Folder, Content) && !FPaths::IsSamePath(Folder, Content);
            Folder = FPaths::GetPath(Folder))
            if (!IFileManager::Get().DeleteDirectory(*Folder, false, false))
                break;
    };
    ON_SCOPE_EXIT
    {
        Cleanup();
    };
    const auto Parse = [](const FString& Text)
    {
        TSharedPtr<FJsonObject> Json;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
        return Json;
    };
    const auto Flag = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        bool Value = false;
        return J.IsValid() && J->TryGetBoolField(Name, Value) && Value;
    };
    const auto Word = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        FString Value;
        if (J.IsValid())
            J->TryGetStringField(Name, Value);
        return Value;
    };
    const auto Import = [&]() { return Parse(UP4AuthoringTools::NativeImportOriginalLevel(Header, FString(), true)); };

    const auto Missing = Parse(UP4AuthoringTools::NativeImportOriginalLevel(Header + TEXT(".none"), FString(), true));
    TestTrue(TEXT("a file that is not there is refused"),
        !Flag(Missing, TEXT("valid")) && Word(Missing, TEXT("error")) == TEXT("level-import-source-unreadable"));

    const auto First = Import();
    if (!TestTrue(TEXT("the level is imported"), Flag(First, TEXT("valid"))))
    {
        AddError(Word(First, TEXT("error")));
        return false;
    }
    TestTrue(TEXT("it was made and saved"), Flag(First, TEXT("created")) && Flag(First, TEXT("saved")));
    const FString State = Word(First, TEXT("state"));
    TestTrue(TEXT("its state is a digest, and is what was recorded"),
        State.Len() == 64 && Word(First, TEXT("imported_state")) == State);
    const TArray<TSharedPtr<FJsonValue>>* Told = nullptr;
    // The build data's id, the saved address and the model's lighting id are kept from the file. The level names its
    // build data once that is imported too, which a level imported alone is not.
    TestTrue(TEXT("what the engine makes for itself is told"),
        First->TryGetArrayField(TEXT("engine_made_and_not_compared"), Told) && Told);
    if (Told)
        for (const auto& Line : *Told)
            AddInfo(TEXT("engine made: ") + Line->AsString());
    TestTrue(TEXT("and the level holds it as the game's file does, but for its build data, not imported here"),
        Told && Told->Num() == 1 && (*Told)[0]->AsString().Contains(TEXT("| mapbuilddata = ")));
    TestTrue(TEXT("eight objects were compared"),
        First->GetIntegerField(TEXT("objects")) == 8 && First->GetIntegerField(TEXT("compared")) > 8);
    UWorld* World = FindObject<UWorld>(nullptr, *Asset);
    if (!TestTrue(
            TEXT("an ordinary level is at the game's path"), World != nullptr && World->PersistentLevel != nullptr))
        return false;
    ULevel* Level = World->PersistentLevel;
    AWorldSettings* Settings = Level->GetWorldSettings(false);
    ALevelScriptActor* Script = Level->GetLevelScriptActor();
    if (!TestTrue(TEXT("it has its world settings and its level script"), Settings != nullptr && Script != nullptr))
        return false;
    const FBoolProperty* Navigation = FindFProperty<FBoolProperty>(
        AWorldSettings::StaticClass(), TEXT("bEnableNavigationSystem"));
    TestTrue(TEXT("the world settings are the game's class, with navigation off as the game's file says"),
        Settings->GetClass()->GetPathName() == TEXT("/Script/Carrot4.MyWorldSettings") && Navigation != nullptr &&
            !Navigation->GetPropertyValue_InContainer(Settings));
    // The level script was compiled again for its variable, which leaves the first script actor's place
    // empty, and the editor may add actors of its own that are never saved.
    bool OnlyThose = Level->Actors.Num() >= 3 && Level->Actors[0] == Settings && Level->Actors[1] != nullptr &&
        Level->Actors[1]->IsA<ABrush>() && Level->Actors.Contains(Script);
    for (int32 Index = 2; OnlyThose && Index < Level->Actors.Num(); ++Index)
        OnlyThose = Level->Actors[Index] == nullptr || Level->Actors[Index] == Script ||
            Level->Actors[Index]->HasAnyFlags(RF_Transient);
    TestTrue(TEXT("its actors are the world settings, the editor's builder brush and the level script"), OnlyThose);
    TestTrue(TEXT("the level script is named as the game's"),
        Script->GetName() == TEXT("Cave000_F00_Sound_C_0") &&
            Script->GetClass()->GetPathName() == Package + TEXT(".Cave000_F00_Sound_C"));
    const FInt64Property* Serial = FindFProperty<FInt64Property>(Script->GetClass(), TEXT("LatestSerialId"));
    TestTrue(TEXT("its class has the game's variable, with the game's default"),
        Serial != nullptr && Serial->GetPropertyValue_InContainer(Script->GetClass()->GetDefaultObject()) == 1 &&
            Serial->GetPropertyValue_InContainer(Script) == 1);
    TestTrue(TEXT("the file is there and the package is not left dirty"),
        IFileManager::Get().FileExists(*File) && !World->GetOutermost()->IsDirty());
    TestTrue(TEXT("its state after the save"), P4LevelState(World) == State);
    const auto Status = Parse(UP4AuthoringTools::NativeOriginalImportDiagnostic(World));
    TestTrue(TEXT("it is known as imported from the game, and as untouched"),
        Flag(Status, TEXT("valid")) && Flag(Status, TEXT("original")) && Flag(Status, TEXT("unchanged")));

    const auto Again = Import();
    TestTrue(TEXT("a second import of the same file changes nothing"),
        Flag(Again, TEXT("valid")) && !Flag(Again, TEXT("created")) && !Flag(Again, TEXT("saved")) &&
            FindObject<UWorld>(nullptr, *Asset) == World);

    Settings->KillZ += 100.0f;
    World->MarkPackageDirty();
    TestTrue(TEXT("a changed setting is seen"),
        !Flag(Parse(UP4AuthoringTools::NativeOriginalImportDiagnostic(World)), TEXT("unchanged")));
    const auto Refused = Import();
    TestTrue(TEXT("an edited level is not imported over"),
        !Flag(Refused, TEXT("valid")) &&
            Word(Refused, TEXT("error")) == TEXT("level-import-would-replace-an-edited-level"));
    Settings->KillZ -= 100.0f;
    TestTrue(TEXT("the setting put back makes it untouched again"),
        Flag(Parse(UP4AuthoringTools::NativeOriginalImportDiagnostic(World)), TEXT("unchanged")));
    Serial->SetPropertyValue_InContainer(Script, 7);
    TestTrue(TEXT("a changed level script value is seen"),
        !Flag(Parse(UP4AuthoringTools::NativeOriginalImportDiagnostic(World)), TEXT("unchanged")));
    Cleanup();
    TestTrue(TEXT("the test leaves nothing"),
        !IFileManager::Get().FileExists(*File) && FindObject<UWorld>(nullptr, *Asset) == nullptr);
    return !HasAnyErrors();
}

// What an edit to an imported level amounts to, and the actors added to it as a level of a mod's own. The
// same level of the game's as above: it has no actor of the game's to take out or move, which the probe
// over the game area covers.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalLevelChangesTest, "P4MapKit.Quality.OriginalLevelChangesAndAddedActors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalLevelChangesTest::RunTest(const FString&)
{
    const FString Package = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Sublevels/Cave000_F00_Sound");
    const FString Asset = Package + TEXT(".Cave000_F00_Sound"), Mine = Package + TEXT("_Added");
    const FString Header = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT")) /
        TEXT(
            "host/Saved/P4MapKit/FullNativeCave000/game/Carrot4/Content/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Sublevels/Cave000_F00_Sound.umap");
    const FString Folder = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/Tests/AddedActors"));
    const FString Cooked = Folder /
        TEXT("Carrot4/Content/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Sublevels/Cave000_F00_Sound_Added");
    FString File;
    FPackageName::TryConvertLongPackageNameToFilename(Package, File, TEXT(".umap"));
    File = FPaths::ConvertRelativePathToFull(File);
    IFileManager::Get().DeleteDirectory(*Folder, false, true);
    if (!TestTrue(TEXT("the game's level file is where the test looks"), IFileManager::Get().FileExists(*Header)) ||
        !TestTrue(TEXT("nothing is at the test's place before it"),
            !IFileManager::Get().FileExists(*File) && FindPackage(nullptr, *Package) == nullptr))
        return false;
    bool Cleaned = false;
    const auto Cleanup = [&]()
    {
        if (Cleaned)
            return;
        Cleaned = true;
        if (UPackage* Made = FindPackage(nullptr, *Package))
        {
            if (UWorld* World = UWorld::FindWorldInPackage(Made))
            {
                FAssetRegistryModule::AssetDeleted(World);
                World->ClearFlags(RF_Public | RF_Standalone);
                World->DestroyWorld(false);
                World->MarkPendingKill();
            }
            ResetLoaders(Made);
            Made->SetDirtyFlag(false);
        }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        IFileManager::Get().Delete(*File, false, true, true);
        for (FString Up = FPaths::GetPath(File);
            FPaths::IsUnderDirectory(Up, Content) && !FPaths::IsSamePath(Up, Content); Up = FPaths::GetPath(Up))
            if (!IFileManager::Get().DeleteDirectory(*Up, false, false))
                break;
        IFileManager::Get().DeleteDirectory(*Folder, false, true);
    };
    ON_SCOPE_EXIT
    {
        Cleanup();
    };
    const auto Parse = [](const FString& Text)
    {
        TSharedPtr<FJsonObject> Json;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
        return Json;
    };
    const auto Flag = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        bool Value = false;
        return J.IsValid() && J->TryGetBoolField(Name, Value) && Value;
    };
    const auto Word = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        FString Value;
        if (J.IsValid())
            J->TryGetStringField(Name, Value);
        return Value;
    };
    const auto Names = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        TArray<FString> Out;
        const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
        if (J.IsValid() && J->TryGetArrayField(Name, List) && List)
            for (const TSharedPtr<FJsonValue>& Value : *List)
                Out.Add(Value->AsString());
        return Out;
    };
    const auto Count = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
        return J.IsValid() && J->TryGetArrayField(Name, List) && List ? List->Num() : -1;
    };
    const auto Changes = [&](UWorld* Of) { return Parse(UP4AuthoringTools::NativeOriginalLevelChanges(Of)); };

    TestTrue(TEXT("no level is refused"), Word(Changes(nullptr), TEXT("error")) == TEXT("level-changes-need-a-level"));
    const auto First = Parse(UP4AuthoringTools::NativeImportOriginalLevel(Header, FString(), true));
    if (!TestTrue(TEXT("the level is imported"), Flag(First, TEXT("valid"))))
    {
        AddError(Word(First, TEXT("error")));
        return false;
    }
    UWorld* World = FindObject<UWorld>(nullptr, *Asset);
    if (!TestTrue(TEXT("the level is there"), World != nullptr && World->PersistentLevel != nullptr))
        return false;

    const auto Untouched = Changes(World);
    TestTrue(TEXT("an untouched level has no change"),
        Flag(Untouched, TEXT("valid")) && !Flag(Untouched, TEXT("edited")) && Count(Untouched, TEXT("removed")) == 0 &&
            Count(Untouched, TEXT("added")) == 0 && Count(Untouched, TEXT("moved")) == 0 &&
            Count(Untouched, TEXT("changed_otherwise")) == 0 && Flag(Untouched, TEXT("held_by_level_changes")) &&
            Word(Untouched, TEXT("level")) == TEXT("Cave000_F00_Sound"));
    TestTrue(TEXT("with nothing added there is nothing to export"),
        P4ExportOriginalLevelActors(World, Mine, Folder).ErrorDetail == TEXT("level-export-nothing-was-added"));

    // A level the kit did not import has nothing to be told against.
    UWorld* Other = UWorld::CreateWorld(
        EWorldType::Inactive, false, TEXT("P4ChangesTestOther"), GetTransientPackage(), false, ERHIFeatureLevel::Num);
    TestTrue(TEXT("a level that was not imported is refused"),
        Other != nullptr &&
            Word(Changes(Other), TEXT("error")) == TEXT("level-changes-need-a-level-imported-from-the-game"));
    if (Other)
    {
        Other->DestroyWorld(false);
        Other->MarkPendingKill();
    }

    FActorSpawnParameters Spawn;
    Spawn.Name = TEXT("P4TestLight");
    Spawn.OverrideLevel = World->PersistentLevel;
    Spawn.ObjectFlags = RF_Transactional;
    const FTransform Place(FRotator::ZeroRotator, FVector(10.0f, 20.0f, 30.0f));
    AActor* Light = World->SpawnActor(APointLight::StaticClass(), &Place, Spawn);
    if (!TestTrue(TEXT("an actor is added"), Light != nullptr))
        return false;
    const auto Added = Changes(World);
    TestTrue(TEXT("the added actor is told, and nothing else"),
        Flag(Added, TEXT("valid")) && Flag(Added, TEXT("edited")) &&
            Names(Added, TEXT("added")) == TArray<FString>({TEXT("P4TestLight")}) &&
            Count(Added, TEXT("removed")) == 0 && Count(Added, TEXT("moved")) == 0 &&
            Count(Added, TEXT("changed_otherwise")) == 0 && Flag(Added, TEXT("held_by_level_changes")));

    TestTrue(TEXT("the mod's level may not be the game's"),
        P4ExportOriginalLevelActors(World, Package, Folder).ErrorDetail == TEXT("level-export-path-unusable"));
    TestTrue(TEXT("a folder is needed"),
        P4ExportOriginalLevelActors(World, Mine, FString()).ErrorDetail == TEXT("level-export-needs-a-folder"));
    TestTrue(TEXT("and it has to be a whole path"),
        P4ExportOriginalLevelActors(World, Mine, TEXT("edit")).ErrorDetail ==
            TEXT("level-export-needs-a-whole-folder-path"));
    UMetaData* Meta = World->GetOutermost()->GetMetaData();
    const TCHAR* DifferentClasses = TEXT("P4NativeLevelClassesNotTheGames");
    Meta->SetValue(World, DifferentClasses, TEXT("/Script/Engine"));
    TestTrue(TEXT("A class used by the exported actor still requires original defaults"),
        P4ExportOriginalLevelActors(World, Mine, Folder, {}, Folder).Error ==
            EP4LevelExportError::ClassDefaultsMismatch);
    // Like Cave033's refused air collision: this class belongs only to untouched originals.
    // No source exists for it, so an attempt to lend every level class would refuse the export.
    Meta->SetValue(World, DifferentClasses, TEXT("/Game/P4Test/UnreconstructibleOriginal"));
    const FString Before = P4LevelState(World);
    const FP4LevelExportResult Export = P4ExportOriginalLevelActors(World, Mine, Folder, {}, Folder);
    if (!TestTrue(TEXT("the added actor is exported"), Export.IsValid()))
    {
        AddError(Export.ErrorDetail);
        return false;
    }
    TestEqual(
        TEXT("Untouched original classes are not reconstructed for a native addition"), Export.LentClasses.Num(), 0);
    Meta->RemoveValue(World, DifferentClasses);
    TestTrue(TEXT("as a level of the mod's own that holds it"),
        Export.Package == Mine && Export.LevelMade && Export.Actors == TArray<FString>({TEXT("P4TestLight")}) &&
            Export.Files.Num() == 2);
    TestTrue(TEXT("the two cooked files lie where the game's content would have them"),
        IFileManager::Get().FileSize(*(Cooked + TEXT(".umap"))) > 0 &&
            IFileManager::Get().FileSize(*(Cooked + TEXT(".uexp"))) > 0);
    TestTrue(TEXT("nothing of that level stays in the project"),
        FindObject<UWorld>(nullptr, *(Mine + TEXT(".Cave000_F00_Sound_Added"))) == nullptr &&
            !FPackageName::DoesPackageExist(Mine));
    TestTrue(TEXT("the editor's level is as it was, with the actor in it"),
        P4LevelState(World) == Before && FindObject<AActor>(World->PersistentLevel, TEXT("P4TestLight")) == Light);

    // Another change to something of the game's is not one a list of level changes holds.
    AWorldSettings* Settings = World->PersistentLevel->GetWorldSettings(false);
    Settings->KillZ += 100.0f;
    const auto Changed = Changes(World);
    TestTrue(TEXT("a changed setting is told as a change the list does not hold"),
        Flag(Changed, TEXT("valid")) && Count(Changed, TEXT("changed_otherwise")) == 1 &&
            !Flag(Changed, TEXT("held_by_level_changes")) &&
            Names(Changed, TEXT("added")) == TArray<FString>({TEXT("P4TestLight")}));
    Settings->KillZ -= 100.0f;
    World->DestroyActor(Light);
    const auto Back = Changes(World);
    TestTrue(TEXT("with the setting put back and the actor gone there is no change"),
        Flag(Back, TEXT("valid")) && !Flag(Back, TEXT("edited")) && Count(Back, TEXT("added")) == 0 &&
            Count(Back, TEXT("changed_otherwise")) == 0);
    Cleanup();
    TestTrue(TEXT("the test leaves nothing"),
        !IFileManager::Get().FileExists(*File) && FindObject<UWorld>(nullptr, *Asset) == nullptr &&
            !IFileManager::Get().DirectoryExists(*Folder));
    return !HasAnyErrors();
}

// Which added actors a lighting build of the mod's own lights: a static mesh actor with a mesh that does
// not move and is tied to no other actor. Every other is told with a sentence that says why.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AddedActorLitTest, "P4MapKit.Quality.AddedActorLitByItsOwnBuild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AddedActorLitTest::RunTest(const FString&)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("the editor has a world"), World) || !TestNotNull(TEXT("the engine's cube is there"), Cube))
        return false;
    AStaticMeshActor* Still = World->SpawnActor<AStaticMeshActor>(FVector(0.0f, 0.0f, 100.0f), FRotator::ZeroRotator);
    AStaticMeshActor* Other = World->SpawnActor<AStaticMeshActor>(FVector(300.0f, 0.0f, 100.0f), FRotator::ZeroRotator);
    APointLight* Light = World->SpawnActor<APointLight>(FVector(0.0f, 0.0f, 500.0f), FRotator::ZeroRotator);
    ON_SCOPE_EXIT
    {
        if (Other)
            World->DestroyActor(Other);
        if (Still)
            World->DestroyActor(Still);
        if (Light)
            World->DestroyActor(Light);
    };
    if (!TestNotNull(TEXT("a mesh actor is made"), Still) || !TestNotNull(TEXT("another is made"), Other) ||
        !TestNotNull(TEXT("a light is made"), Light))
        return false;
    const FString Tied(TEXT("It is tied to another actor of the level."));
    TestEqual(TEXT("no actor"), P4WhyNotLitByABuildOfTheMods(nullptr), FString(TEXT("It is not in the level.")));
    TestEqual(TEXT("a light is not a mesh actor"), P4WhyNotLitByABuildOfTheMods(Light),
        FString(TEXT("It is not a static mesh actor.")));
    TestEqual(
        TEXT("a mesh actor without a mesh"), P4WhyNotLitByABuildOfTheMods(Still), FString(TEXT("It has no mesh.")));
    Still->GetStaticMeshComponent()->SetStaticMesh(Cube);
    Other->GetStaticMeshComponent()->SetStaticMesh(Cube);
    TestTrue(
        TEXT("a mesh actor that does not move is lit by the build"), P4WhyNotLitByABuildOfTheMods(Still).IsEmpty());
    Still->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    TestEqual(TEXT("a mesh that can move"), P4WhyNotLitByABuildOfTheMods(Still),
        FString(TEXT("Its mesh can move, and the game lights what moves by itself.")));
    Still->GetStaticMeshComponent()->SetMobility(EComponentMobility::Stationary);
    TestEqual(TEXT("a mesh that can change without moving"), P4WhyNotLitByABuildOfTheMods(Still),
        FString(TEXT("Its mesh can move, and the game lights what moves by itself.")));
    Still->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
    Other->AttachToActor(Still, FAttachmentTransformRules::KeepWorldTransform);
    TestEqual(TEXT("an actor with another attached to it"), P4WhyNotLitByABuildOfTheMods(Still), Tied);
    TestEqual(TEXT("an actor attached to another"), P4WhyNotLitByABuildOfTheMods(Other), Tied);
    Other->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    TestTrue(TEXT("apart again, both are lit by the build"),
        P4WhyNotLitByABuildOfTheMods(Still).IsEmpty() && P4WhyNotLitByABuildOfTheMods(Other).IsEmpty());
    return !HasAnyErrors();
}

// A map's bake setup put into a level and taken out again: the lights the kit's bake puts in, under its
// labels, with the values the setup gives, and nothing of it left afterwards.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4LightingBakeSetupTest, "P4MapKit.Quality.LightingBakeSetup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4LightingBakeSetupTest::RunTest(const FString&)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
    if (!TestNotNull(TEXT("the editor has a world"), World) || !TestNotNull(TEXT("the world has settings"), Settings))
        return false;
    const FLightmassWorldInfoSettings LightmassBefore = Settings->LightmassSettings;
    const bool ForcedBefore = Settings->bForceNoPrecomputedLighting;
    const FString SkyFile = FPackageName::LongPackageNameToFilename(
        TEXT("/Game/P4MapKit/Bake/MI_P4BakeSky"), FPackageName::GetAssetPackageExtension());
    ON_SCOPE_EXIT
    {
        UP4AuthoringTools::NativeLightingBakeClear(World);
        Settings->LightmassSettings = LightmassBefore;
        Settings->bForceNoPrecomputedLighting = ForcedBefore;
        IFileManager::Get().Delete(*SkyFile, false, true);
    };
    const auto Parse = [](const FString& Text)
    {
        TSharedPtr<FJsonObject> Json;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
        return Json;
    };
    const auto Flag = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        bool Value = false;
        return J.IsValid() && J->TryGetBoolField(Name, Value) && Value;
    };
    const auto Word = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        FString Value;
        if (J.IsValid())
            J->TryGetStringField(Name, Value);
        return Value;
    };
    const auto Words = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        TArray<FString> Out;
        const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
        if (J.IsValid() && J->TryGetArrayField(Name, List) && List)
            for (const TSharedPtr<FJsonValue>& Value : *List)
                Out.Add(Value->AsString());
        return Out;
    };
    const auto Labelled = [&](const TCHAR* Label) -> AActor*
    {
        for (AActor* Actor : World->PersistentLevel->Actors)
            if (Actor && !Actor->IsPendingKill() && Actor->GetActorLabel() == Label)
                return Actor;
        return nullptr;
    };
    const auto BakeActors = [&]()
    {
        int32 Count = 0;
        for (AActor* Actor : World->PersistentLevel->Actors)
            if (Actor && !Actor->IsPendingKill() && Actor->GetActorLabel().StartsWith(TEXT("P4Bake")))
                ++Count;
        return Count;
    };
    const FString Sun = TEXT(
        R"("sun":{"rotation":[-90,0,0],"intensity":0.5,"color":{"R":1.0,"G":0.5,"B":0.0},"indirect_intensity":2.0,"direct":false})");
    const FString Setup = FString(TEXT("{")) + Sun + TEXT(R"(,
"game_sun":{"rotation":[-40,30,0],"intensity":3.0,"color":[10,20,30,255],"source_angle":1.5,"use_temperature":true,"temperature":5000},
"lights":[{"kind":"PointLight","location":[100,200,300],"rotation":[0,0,0],"color":[255,128,0,255],"units":"CANDELAS",
           "values":{"intensity":7.0,"attenuation_radius":450.0,"no_such_value":1}}],
"importance_volumes":[{"centre":[10,20,30],"size":[4000,2000,800]}],
"sky":{"cubemap":"/Engine/MapTemplates/Sky/DaylightAmbientCubemap","game_cubemap":"/Game/No/Such/Cubemap","intensity":100.0,"indirect_intensity":1.5,
       "location":[1,2,3],"lower_hemisphere_black":true,"distance_threshold":90000.0,"capture_emissive_only":true},
"lightmass":{"StaticLightingLevelScale":0.25,"NumIndirectLightingBounces":10,"bUseAmbientOcclusion":true,
             "VolumeLightingMethod":"EVolumeLightingMethod::VLM_SparseVolumeLightingSamples","EnvironmentColor":{"R":1.0,"G":1.0,"B":1.0},
             "NoSuchSetting":3,"Nested":{"a":1}}})");
    TestEqual(TEXT("no level is refused"),
        Word(Parse(UP4AuthoringTools::NativeLightingBakeSetup(nullptr, Setup)), TEXT("error")),
        FString(TEXT("lighting-bake-needs-a-level")));
    TestEqual(TEXT("a setup that cannot be read is refused"),
        Word(Parse(UP4AuthoringTools::NativeLightingBakeSetup(World, TEXT("not json"))), TEXT("error")),
        FString(TEXT("lighting-bake-setup-unreadable")));
    TestEqual(TEXT("a setup without a sky light is refused"),
        Word(Parse(UP4AuthoringTools::NativeLightingBakeSetup(World, FString(TEXT("{")) + Sun + TEXT("}"))),
            TEXT("error")),
        FString(TEXT("lighting-bake-setup-has-no-whole-sky-light")));
    TestEqual(TEXT("and leaves nothing of itself in the level"), BakeActors(), 0);
    const auto Said = Parse(UP4AuthoringTools::NativeLightingBakeSetup(World, Setup));
    if (!TestTrue(TEXT("the setup is put in"), Flag(Said, TEXT("valid"))))
    {
        AddError(Word(Said, TEXT("error")));
        return false;
    }
    TestEqual(TEXT("what was put in is named"), FString::Join(Words(Said, TEXT("placed")), TEXT(",")),
        FString(TEXT("P4BakeLight,P4BakeSky,P4BakeSun,P4BakeSunOfTheGame,P4BakeVolume")));
    TestEqual(TEXT("the sky light takes the first cubemap the project has, and says which"),
        Word(Said, TEXT("sky_source")), FString(TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap")));
    TestEqual(TEXT("a value no property takes is told, and a nested setting is not one"),
        FString::Join(Words(Said, TEXT("values_not_set")), TEXT(",")),
        FString(TEXT("PointLight.no_such_value,lightmass.NoSuchSetting")));
    const ADirectionalLight* BakeSun = Cast<ADirectionalLight>(Labelled(TEXT("P4BakeSun")));
    const ADirectionalLight* GameSun = Cast<ADirectionalLight>(Labelled(TEXT("P4BakeSunOfTheGame")));
    const APointLight* Point = Cast<APointLight>(Labelled(TEXT("P4BakeLight")));
    const ASkyLight* Sky = Cast<ASkyLight>(Labelled(TEXT("P4BakeSky")));
    const ALightmassImportanceVolume* Volume = Cast<ALightmassImportanceVolume>(Labelled(TEXT("P4BakeVolume")));
    if (!TestTrue(TEXT("each is an actor of its kind"), BakeSun && GameSun && Point && Sky && Volume))
        return false;
    const ULightComponent* SunLight = BakeSun->GetLightComponent();
    TestTrue(TEXT("the sun is one whose bounce alone is baked"), SunLight->Mobility == EComponentMobility::Stationary);
    TestTrue(TEXT("the sun has the setup's strength, direct and bounced"),
        SunLight->Intensity == 0.5f && SunLight->IndirectLightingIntensity == 2.0f);
    TestEqual(TEXT("the sun's color is the setup's, as the bytes the kit writes"), SunLight->LightColor.ToString(),
        FColor(255, 188, 0, 255).ToString());
    TestTrue(TEXT("the sun points where the setup says"),
        BakeSun->GetActorForwardVector().Equals(FVector(0.0f, 0.0f, -1.0f), 0.001f));
    const UDirectionalLightComponent* Own = Cast<UDirectionalLightComponent>(GameSun->GetLightComponent());
    TestTrue(TEXT("the game's own sun points where the setup says"),
        GameSun->GetActorForwardVector().Equals(FRotator(-40.0f, 30.0f, 0.0f).Vector(), 0.001f));
    TestTrue(TEXT("the game's own sun casts baked shadows and has its color, angle and temperature"),
        Own && Own->Mobility == EComponentMobility::Stationary && Own->Intensity == 3.0f &&
            Own->LightColor == FColor(30, 20, 10, 255) && Own->CastShadows && Own->CastStaticShadows &&
            Own->LightSourceAngle == 1.5f && Own->bUseTemperature && Own->Temperature == 5000.0f &&
            Own->IndirectLightingIntensity == 1.0f);
    const UPointLightComponent* Lamp = Cast<UPointLightComponent>(Point->GetLightComponent());
    TestTrue(TEXT("a static light is all baked, where and as the setup says"),
        Lamp && Lamp->Mobility == EComponentMobility::Static && Lamp->LightColor == FColor(255, 128, 0, 255) &&
            Lamp->IntensityUnits == ELightUnits::Candelas && Lamp->Intensity == 7.0f &&
            Lamp->AttenuationRadius == 450.0f &&
            Point->GetActorLocation().Equals(FVector(100.0f, 200.0f, 300.0f), 0.01f));
    TestTrue(TEXT("the importance volume has the setup's place and size"),
        Volume->GetActorLocation().Equals(FVector(10.0f, 20.0f, 30.0f), 0.01f) &&
            Volume->GetActorScale3D().Equals(FVector(20.0f, 10.0f, 4.0f), 0.001f));
    const USkyLightComponent* SkyLight = Sky->GetLightComponent();
    TestTrue(TEXT("the sky light has the cubemap and the setup's values"),
        SkyLight->Mobility == EComponentMobility::Stationary && SkyLight->SourceType == SLS_SpecifiedCubemap &&
            SkyLight->Cubemap && SkyLight->Intensity == 100.0f && SkyLight->IndirectLightingIntensity == 1.5f &&
            SkyLight->bLowerHemisphereIsBlack && SkyLight->SkyDistanceThreshold == 90000.0f &&
            SkyLight->bCaptureEmissiveOnly && Sky->GetActorLocation().Equals(FVector(1.0f, 2.0f, 3.0f), 0.01f));
    const FLightmassWorldInfoSettings& Lightmass = Settings->LightmassSettings;
    TestTrue(TEXT("the level's Lightmass settings are the setup's over the engine's own"),
        !Settings->bForceNoPrecomputedLighting && Lightmass.StaticLightingLevelScale == 0.25f &&
            Lightmass.NumIndirectLightingBounces == 10 && Lightmass.bUseAmbientOcclusion &&
            Lightmass.VolumeLightingMethod == VLM_SparseVolumeLightingSamples &&
            Lightmass.EnvironmentColor == FColor(255, 255, 255, 255) &&
            Lightmass.IndirectLightingQuality == FLightmassWorldInfoSettings().IndirectLightingQuality);
    TestEqual(TEXT("a level that has a setup is not given another"),
        Word(Parse(UP4AuthoringTools::NativeLightingBakeSetup(World, Setup)), TEXT("error")).Left(39),
        FString(TEXT("lighting-bake-level-has-a-setup-already")));
    const auto Cleared = Parse(UP4AuthoringTools::NativeLightingBakeClear(World));
    TestTrue(TEXT("the setup is taken out again, all of it"),
        Flag(Cleared, TEXT("valid")) && Words(Cleared, TEXT("taken_out")).Num() == 5 &&
            Words(Cleared, TEXT("left")).Num() == 0 && BakeActors() == 0);
    // A sky that is captured: a copy of the map's sky sphere with the sky's values on an instance of its material.
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EngineSky/SM_SkySphere.SM_SkySphere"));
    if (TestNotNull(TEXT("the engine's sky sphere is there"), Sphere) &&
        TestNotNull(TEXT("and has a material"), Sphere->GetMaterial(0)))
    {
        const FString Captured = FString(TEXT("{")) + Sun + TEXT(R"(,"sky":{"intensity":2.0,"indirect_intensity":1.0,
"capture":{"mesh":"/Engine/EngineSky/SM_SkySphere","location":[0,0,-100],"rotation":[0,0,0],"scale":[4,4,4],"params":{"Zenith Color":[0.1,0.2,0.3,1.0],"Cloud speed":2.0}}}})");
        const auto Second = Parse(UP4AuthoringTools::NativeLightingBakeSetup(World, Captured));
        if (TestTrue(TEXT("a setup that captures its sky is put in"), Flag(Second, TEXT("valid"))))
        {
            const ASkyLight* Capturing = Cast<ASkyLight>(Labelled(TEXT("P4BakeSky")));
            const AStaticMeshActor* Copy = Cast<AStaticMeshActor>(Labelled(TEXT("P4BakeSkySphere")));
            const UMaterialInstanceConstant* Instance = Copy
                ? Cast<UMaterialInstanceConstant>(Copy->GetStaticMeshComponent()->GetMaterial(0))
                : nullptr;
            FLinearColor Zenith(ForceInit);
            float Speed = 0.0f;
            TestTrue(TEXT("the sky light captures the scene, and says so"),
                Capturing && Capturing->GetLightComponent()->SourceType == SLS_CapturedScene &&
                    Word(Second, TEXT("sky_source")) == TEXT("captured"));
            TestTrue(
                TEXT(
                    "the sphere is the map's mesh, still, without shadow, with an instance of its material that has the sky's values"),
                Copy && Instance && Copy->GetStaticMeshComponent()->GetStaticMesh() == Sphere &&
                    Copy->GetStaticMeshComponent()->Mobility == EComponentMobility::Static &&
                    !Copy->GetStaticMeshComponent()->CastShadow && Instance->Parent == Sphere->GetMaterial(0) &&
                    Instance->GetVectorParameterValue(FName(TEXT("Zenith Color")), Zenith) &&
                    Zenith.Equals(FLinearColor(0.1f, 0.2f, 0.3f, 1.0f), 0.0001f) &&
                    Instance->GetScalarParameterValue(FName(TEXT("Cloud speed")), Speed) && Speed == 2.0f);
            TestTrue(TEXT("the instance is an asset of the project, for the build's own editor to find"),
                Word(Second, TEXT("sky_material")) == TEXT("/Game/P4MapKit/Bake/MI_P4BakeSky") &&
                    IFileManager::Get().FileExists(*SkyFile));
            TestTrue(TEXT("and the sphere is taken out with the rest"),
                Flag(Parse(UP4AuthoringTools::NativeLightingBakeClear(World)), TEXT("valid")) && BakeActors() == 0);
        }
        else
            AddError(Word(Second, TEXT("error")));
    }
    return !HasAnyErrors();
}

// The collision of an added actor as the kit's navigation bake reads it: the engine's own answer for a
// cube, where the cube is, with its top going round the way ground goes round in the kit's files (the
// right-hand normal up in the file's own numbers) and its underside the other way. A file with the
// triangles the other way round gave a navigation with the underside of a cube as ground.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActorNavigationCollisionTest, "P4MapKit.Quality.AddedActorNavigationCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActorNavigationCollisionTest::RunTest(const FString&)
{
    const FString Folder = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/Tests/NavigationCollision"));
    const FString File = Folder / TEXT("Cube.obj");
    IFileManager::Get().DeleteDirectory(*Folder, false, true);
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("the editor has a world"), World) || !TestNotNull(TEXT("the engine's cube is there"), Cube))
        return false;
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
        FVector(100.0f, 200.0f, 300.0f), FRotator::ZeroRotator);
    APointLight* Light = World->SpawnActor<APointLight>(FVector(0.0f, 0.0f, 500.0f), FRotator::ZeroRotator);
    ON_SCOPE_EXIT
    {
        if (Actor)
            World->DestroyActor(Actor);
        if (Light)
            World->DestroyActor(Light);
        IFileManager::Get().DeleteDirectory(*Folder, false, true);
    };
    if (!TestNotNull(TEXT("a mesh actor is made"), Actor) || !TestNotNull(TEXT("a light is made"), Light))
        return false;
    Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
    const auto Parse = [](const FString& Text)
    {
        TSharedPtr<FJsonObject> Json;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
        return Json;
    };
    const auto Flag = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        bool Value = false;
        return J.IsValid() && J->TryGetBoolField(Name, Value) && Value;
    };
    const auto Word = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        FString Value;
        if (J.IsValid())
            J->TryGetStringField(Name, Value);
        return Value;
    };
    const auto Number = [](const TSharedPtr<FJsonObject>& J, const TCHAR* Name)
    {
        double Value = -1.0;
        if (J.IsValid())
            J->TryGetNumberField(Name, Value);
        return int32(Value);
    };
    TestEqual(TEXT("no actor is refused"),
        Word(Parse(UP4AuthoringTools::NativeWriteActorNavigationCollision(nullptr, File)), TEXT("error")),
        FString(TEXT("navigation-collision-needs-an-actor")));
    TestEqual(TEXT("a file that is not a whole path is refused"),
        Word(Parse(UP4AuthoringTools::NativeWriteActorNavigationCollision(Actor, TEXT("cube.obj"))), TEXT("error")),
        FString(TEXT("navigation-collision-needs-a-whole-path")));
    TestEqual(TEXT("no file is refused"),
        Word(Parse(UP4AuthoringTools::NativeWriteActorNavigationCollision(Actor, FString())), TEXT("error")),
        FString(TEXT("navigation-collision-needs-a-file")));
    TestEqual(TEXT("an actor navigation does not take into account has none"),
        Word(Parse(UP4AuthoringTools::NativeWriteActorNavigationCollision(Light, File)), TEXT("error")),
        FString(TEXT("navigation-collision-the-actor-has-none")));
    TestTrue(TEXT("and no file is written for it"), !IFileManager::Get().FileExists(*File));
    const auto Said = Parse(UP4AuthoringTools::NativeWriteActorNavigationCollision(Actor, File));
    if (!TestTrue(TEXT("the cube's collision is written"),
            Flag(Said, TEXT("valid")) && IFileManager::Get().FileExists(*File)))
    {
        AddError(Word(Said, TEXT("error")));
        return false;
    }
    TestTrue(TEXT("one component, a box of eight points and twelve triangles"),
        Number(Said, TEXT("components")) == 1 && Number(Said, TEXT("vertices")) == 8 &&
            Number(Said, TEXT("triangles")) == 12);
    TArray<FString> Lines;
    FFileHelper::LoadFileToStringArray(Lines, *File);
    TArray<FVector> Points;
    int32 Up = 0, Down = 0, Sides = 0, Bad = 0;
    FBox Bounds(ForceInit);
    for (const FString& Line : Lines)
    {
        TArray<FString> Words;
        Line.ParseIntoArrayWS(Words);
        if (Words.Num() == 4 && Words[0] == TEXT("v"))
        {
            Points.Add(FVector(FCString::Atof(*Words[1]), FCString::Atof(*Words[2]), FCString::Atof(*Words[3])));
            Bounds += Points.Last();
        }
        else if (Words.Num() == 4 && Words[0] == TEXT("f"))
        {
            const int32 A = FCString::Atoi(*Words[1]) - 1, B = FCString::Atoi(*Words[2]) - 1,
                        C = FCString::Atoi(*Words[3]) - 1;
            if (!Points.IsValidIndex(A) || !Points.IsValidIndex(B) || !Points.IsValidIndex(C))
            {
                ++Bad;
                continue;
            }
            // The right-hand normal in the file's own numbers, worked out here and not by the engine, whose
            // cross product is for its own left-handed coordinates.
            const FVector E = Points[B] - Points[A], F = Points[C] - Points[A];
            const float Z = E.X * F.Y - E.Y * F.X;
            const float Lowest = FMath::Min3(Points[A].Z, Points[B].Z, Points[C].Z),
                        Highest = FMath::Max3(Points[A].Z, Points[B].Z, Points[C].Z);
            if (Lowest > 349.0f)
            {
                if (Z > 0.0f)
                    ++Up;
                else
                    ++Bad;
            }
            else if (Highest < 251.0f)
            {
                if (Z < 0.0f)
                    ++Down;
                else
                    ++Bad;
            }
            else
                ++Sides;
        }
    }
    TestTrue(TEXT("the file is for Unreal's importer, which mirrors Y: the cube is at Y -200 in it"),
        Bounds.Min.Equals(FVector(50.0f, -250.0f, 250.0f), 0.01f) &&
            Bounds.Max.Equals(FVector(150.0f, -150.0f, 350.0f), 0.01f));
    TestTrue(TEXT("the top goes round as ground does, the underside the other way, and the eight others are sides"),
        Up == 2 && Down == 2 && Sides == 8 && Bad == 0);
    return !HasAnyErrors();
}
#endif
