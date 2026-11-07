#if P4_MAPKIT_TESTS
#include "P4StaticMeshSockets.h"
#include "P4StaticMeshContent.h"
#include "P4OriginalMeshImport.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalMeshSockets, "P4MapKit.Mesh.OriginalOwnedSockets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalMeshSockets::RunTest(const FString&)
{
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_STATIC_MESH_SOCKET_SOURCE"));
    if (Root.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned socket-bearing original meshes."));
        return true;
    }
    for (const TCHAR* Name : {TEXT("SM_stg_Area003_SandHill01"), TEXT("SM_stg_Area003_SandHill01_Soft")})
    {
        const FString Package =
            TEXT("/Game/Carrot4/Terrain/Area/Area003/Objects/stg_Area003_SandHill01/") + FString(Name);
        const FString Header = Root / (Package.Mid(6) + TEXT(".uasset"));
        const FString BodyPath = FPaths::ChangeExtension(Header, TEXT("uexp"));
        FP4OriginalMeshSource Source;
        FString Error;
        if (!TestTrue(TEXT("The complete socket-bearing source decodes: ") + Error,
                      P4DecodeOriginalMesh(Header, BodyPath, FString(), Source, Error)))
            return false;
        if (!TestEqual(TEXT("The actual source has one qualified owned socket"), Source.SocketProperties.Num(), 1))
            return false;
        TArray<uint8> HeaderBytes, BodyBytes;
        if (!TestTrue(TEXT("The exact source buffers remain readable"),
                      FFileHelper::LoadFileToArray(HeaderBytes, *Header) &&
                          FFileHelper::LoadFileToArray(BodyBytes, *BodyPath)))
            return false;
        const std::vector<uint8> H(HeaderBytes.GetData(), HeaderBytes.GetData() + HeaderBytes.Num());
        const std::vector<uint8> B(BodyBytes.GetData(), BodyBytes.GetData() + BodyBytes.Num());
        const auto Inventory = P4MapKit::ReadPackage(H, B);
        const int32 SocketIndex = Source.Render.SocketIndices[0];
        const int32 MeshIndex = Inventory.Exports[size_t(SocketIndex - 1)].Outer;
        const auto& MeshExport = Inventory.Exports[size_t(MeshIndex - 1)];
        const auto MeshTags =
            P4MapKit::ReadPropertyEnvelopes(B, Inventory.Names, MeshExport.Offset, MeshExport.Offset + MeshExport.Size);
        const P4MapKit::FPropertyEnvelope* SocketTag = nullptr;
        for (const auto& Tag : MeshTags.Properties)
            if (Tag.Name == "Sockets")
                SocketTag = &Tag;
        if (!TestTrue(TEXT("The actual source has both tagged and native socket lists"),
                      SocketTag && SocketTag->Size == 8))
            return false;
        for (int32 Control = 0; Control < 5; ++Control)
        {
            auto Changed = Inventory;
            auto Bytes = B;
            auto Indices = Source.Render.SocketIndices;
            auto& Export = Changed.Exports[size_t(SocketIndex - 1)];
            if (Control == 0)
                Export.Outer = 0;
            if (Control == 1)
                Export.Class = Changed.Exports[size_t(MeshIndex - 1)].Class;
            if (Control == 2)
                Bytes[Export.Offset + Export.Size - 1] = 1;
            if (Control == 3)
                Indices.push_back(SocketIndex);
            if (Control == 4)
                Bytes[SocketTag->PayloadOffset + 4] ^= 1;
            TArray<FP4OriginalMeshSocket> Held;
            Held.AddDefaulted();
            Held[0].ObjectName = TEXT("UnrelatedExistingResult");
            TestFalse(
                TEXT("Unowned, wrong-class, opaque-tail, duplicate and conflicting socket sources remain refused"),
                P4ReadOriginalMeshSockets(Changed, Bytes, MeshIndex, Indices, Held, Error));
            TestTrue(TEXT("A failed socket decode preserves its caller's result"),
                     Held.Num() == 1 && Held[0].ObjectName == TEXT("UnrelatedExistingResult"));
        }
        const auto Imported = P4ValidateReconstructedMeshImport(Header, BodyPath, true);
        if (!TestTrue(TEXT("The normal mesh publication path imports sockets: ") + Imported.ErrorDetail,
                      Imported.IsValid()))
            return false;
        auto* Mesh = LoadObject<UStaticMesh>(nullptr, *(Package + TEXT(".") + Name));
        if (!TestNotNull(TEXT("The newly published mesh is available"), Mesh))
            return false;
        UPackage* SavedPackage = Mesh->GetOutermost();
        Mesh = nullptr;
        FText UnloadError;
        if (!TestTrue(TEXT("The saved socket mesh unloads before a cold asset load"),
                      UPackageTools::UnloadPackages({SavedPackage}, UnloadError)))
            return false;
        Mesh = LoadObject<UStaticMesh>(nullptr, *(Package + TEXT(".") + Name));
        if (!TestTrue(TEXT("The saved ordinary mesh retains its socket"),
                      Mesh && Mesh->Sockets.Num() == 1 && Mesh->Sockets[0]))
            return false;
        UStaticMeshSocket* Socket = Mesh->Sockets[0];
        TestTrue(TEXT("The socket is owned by the imported mesh and retains its source name"),
                 Socket->GetOuter() == Mesh && Socket->SocketName == FName(TEXT("Socket")));
        TestTrue(TEXT("The socket-bearing mesh is unchanged after publication"), P4OriginalMeshBaselineUnchanged(Mesh));
        const FVector Location = Socket->RelativeLocation;
        const bool WasDirty = Mesh->GetOutermost()->IsDirty();
        ON_SCOPE_EXIT
        {
            Socket->RelativeLocation = Location;
            Mesh->GetOutermost()->SetDirtyFlag(WasDirty);
        };
        Socket->RelativeLocation.X += 1.f;
        Mesh->MarkPackageDirty();
        TestFalse(TEXT("A real socket edit changes the original mesh baseline"), P4OriginalMeshBaselineUnchanged(Mesh));
        const auto Refused = P4ValidateReconstructedMeshImport(Header, BodyPath, true);
        TestFalse(TEXT("Reimport preserves an unsaved creator socket edit"), Refused.IsValid());
        TestTrue(TEXT("The creator socket location remains exactly entered"),
                 Socket->RelativeLocation.X == Location.X + 1.f);
        Socket->RelativeLocation = Location;
        Mesh->GetOutermost()->SetDirtyFlag(WasDirty);
        TestTrue(TEXT("Restoring the original socket restores exact baseline equality"),
                 P4OriginalMeshBaselineUnchanged(Mesh));
    }
    return !HasAnyErrors();
}
#endif
