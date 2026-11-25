#if P4_MAPKIT_TESTS
#include "P4ExportDriverTestShared.h"
#include "P4GateWallPreview.h"
#include "P4NativeConstructionPreview.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

// A gate of Cave002_F00 drawn by the gate wall preview: its pieces are the ones the game's editor saved in the
// level for its length, and a new length gives the pieces, far side, boxes and inside collision the game builds.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GateWallPreviewTest, "P4MapKit.Editor.GateWallPreviewCave002",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GateWallPreviewTest::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    const TCHAR* const Cave002 = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave002/Cave002_F00/Cave002_F00_P");
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty() || FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")).IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. GateWallPreviewCave002 was not exercised. Set P4_NATIVE_ROOT and ")
                       TEXT("P4_NATIVE_ORIGINAL_CONTAINERS."));
        return true;
    }
    FP4ExportModRequest Request;
    FP4ModNames Names;
    if (!P4ExportDriverTest::Request(*this, TEXT("native.gate.test"), TEXT("Native Gate Test"), Request, Names))
        return false;
    // A project that has the cave keeps it; otherwise the panel's importer brings it in from the game's containers
    // and everything it made is taken out again at the end.
    TUniquePtr<FImported> Imported;
    if (!FPackageName::DoesPackageExist(Cave002))
    {
        Imported = MakeUnique<FImported>(TEXT("GateWallPreviewCave002"));
        if (!TestTrue(TEXT("the test can keep its import cleanup list"), Imported->Ready))
            return false;
        FP4KadanImportSettings Settings;
        Settings.GameFolder = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
        Settings.GamePaks = Request.GamePaksDirectory;
        Settings.Retoc = Request.Tools.Retoc;
        Settings.Repak = Request.Tools.Repak;
        Settings.AllowGameBackedLevels = true;
        const FP4KadanImportResult Import = P4KadanImportMap(Settings, Cave002, true);
        for (const FString& Problem : Import.Problems)
            AddInfo(TEXT("Not imported. ") + Problem);
        if (!KeepImport(*this, TEXT("GateWallPreviewCave002"), Import))
            return false;
    }
    if (!TestTrue(TEXT("the project has Cave002_F00"), FPackageName::DoesPackageExist(Cave002)))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Cave002);
    if (!TestNotNull(TEXT("the map opens in the editor"), World))
        return false;
    AActor* Gate = nullptr;
    for (ULevel* Level : World->GetLevels())
        for (AActor* Actor : Level ? Level->Actors : TArray<AActor*>())
            if (P4IsGateWall(Actor) && Actor->GetClass()->GetName().StartsWith(TEXT("GVarGateDenkiNoPillar")))
                Gate = Gate ? Gate : Actor;
    if (!TestNotNull(TEXT("the cave's electric gate is in the map"), Gate))
        return false;
    FIntProperty* Length = FindFProperty<FIntProperty>(Gate->GetClass(), TEXT("AddWallNum"));
    if (!TestNotNull(TEXT("the gate has its length option"), Length))
        return false;
    const int32 Saved = Length->GetPropertyValue_InContainer(Gate);
    AddInfo(FString::Printf(TEXT("%s, AddWallNum %d"), *Gate->GetPathName(), Saved));
    TestEqual(TEXT("the gate's saved length is the cave's"), Saved, 4);

    struct FPiece
    {
        FString Tags, Mesh, Parent;
        FVector Location;
    };
    const auto Describe = [](UStaticMeshComponent* Mesh)
    {
        FPiece Out;
        TArray<FString> Tags;
        for (const FName& Tag : Mesh->ComponentTags)
            Tags.Add(Tag.ToString());
        Out.Tags = FString::Join(Tags, TEXT(","));
        Out.Mesh = Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetPathName() : FString();
        Out.Parent = Mesh->GetAttachParent() ? Mesh->GetAttachParent()->GetName() : FString();
        Out.Location = Mesh->GetRelativeLocation();
        return Out;
    };
    const auto Line = [](const FPiece& Piece)
    {
        return FString::Printf(TEXT("%s %s on %s at %s"), *Piece.Tags, *FPaths::GetBaseFilename(Piece.Mesh),
            *Piece.Parent, *Piece.Location.ToString());
    };
    const auto Sorted = [](TArray<FPiece> Pieces)
    {
        Pieces.Sort([](const FPiece& A, const FPiece& B) { return A.Tags < B.Tags; });
        return Pieces;
    };
    const auto Same = [&](const TArray<FPiece>& A, const TArray<FPiece>& B, const TCHAR* What)
    {
        bool Equal = A.Num() == B.Num();
        for (int32 Index = 0; Equal && Index < A.Num(); ++Index)
            Equal = A[Index].Tags == B[Index].Tags && A[Index].Mesh == B[Index].Mesh &&
                A[Index].Parent == B[Index].Parent && A[Index].Location.Equals(B[Index].Location, 0.01f);
        for (const FPiece& Piece : A)
            AddInfo(FString(What) + TEXT(" has ") + Line(Piece));
        for (const FPiece& Piece : B)
            AddInfo(FString(What) + TEXT(" expects ") + Line(Piece));
        return TestTrue(What, Equal);
    };
    const auto Component = [&](const TCHAR* Tag) -> USceneComponent*
    {
        TInlineComponentArray<USceneComponent*> Parts(Gate);
        for (USceneComponent* Part : Parts)
            if (Part && Part->ComponentHasTag(FName(Tag)) && !P4GateWallPreviewPieces(Gate).Contains(Part))
                return Part;
        return nullptr;
    };

    const auto SavedPieces = [&]()
    {
        TArray<FPiece> Pieces;
        TInlineComponentArray<UStaticMeshComponent*> Meshes(Gate);
        for (UStaticMeshComponent* Mesh : Meshes)
            if (Mesh && Mesh->ComponentHasTag(TEXT("AddWall")))
                Pieces.Add(Describe(Mesh));
        return Sorted(MoveTemp(Pieces));
    };

    // Import can rebuild construction-script components. Keep the editor's saved state to check that
    // suspending the preview restores it, and check the drawn pieces against the original layout separately.
    TArray<FPiece> Level;
    FVector FarSide, NavAt, NavExtent, LaterAt, LaterExtent, InsideScale;
    {
        FP4SuspendNativePreviews Out;
        Level = SavedPieces();
        USceneComponent* Far = Component(TEXT("OutSideR"));
        UBoxComponent* Nav = Cast<UBoxComponent>(Component(TEXT("Nav_Box")));
        UBoxComponent* Later = Cast<UBoxComponent>(Component(TEXT("LaterTaskTrigger")));
        USceneComponent* Inside = Component(TEXT("Col_Inside"));
        if (!TestTrue(TEXT("the gate has its far side, boxes and inside collision"), Far && Nav && Later && Inside))
            return false;
        FarSide = Far->GetRelativeLocation();
        NavAt = Nav->GetRelativeLocation();
        NavExtent = Nav->GetUnscaledBoxExtent();
        LaterAt = Later->GetRelativeLocation();
        LaterExtent = Later->GetUnscaledBoxExtent();
        InsideScale = Inside->GetRelativeScale3D();
        TestTrue(TEXT("the preview's pieces are out while the previews are"), P4GateWallPreviewPieces(Gate).Num() == 0);
    }
    // At this length the game builds two width-two pieces and stretches the inside collision over five units.
    const float Width = FindFProperty<FFloatProperty>(Gate->GetClass(), TEXT("WallWidth"))->GetPropertyValue_InContainer(Gate);
    TestTrue(TEXT("the map opened with the gate drawn"), P4ApplyGateWallPreview(Gate));
    TArray<FPiece> Drawn;
    for (UStaticMeshComponent* Piece : P4GateWallPreviewPieces(Gate))
        Drawn.Add(Describe(Piece));
    Drawn = Sorted(MoveTemp(Drawn));
    if (TestEqual(TEXT("the original length draws two wall pieces"), Drawn.Num(), 2))
    {
        for (int32 Index = 0; Index < Drawn.Num(); ++Index)
        {
            const FPiece& Piece = Drawn[Index];
            TestEqual(TEXT("the original wall piece has the width-two tags"), Piece.Tags,
                FString::Printf(TEXT("Wall_1_%d,Width_2,AddWall"), Index));
            TestTrue(TEXT("the original part hangs on the first unit and has a mesh"),
                Piece.Parent.Contains(TEXT("Mesh_Inside_00")) && !Piece.Mesh.IsEmpty());
            TestTrue(TEXT("the original piece is where the game places it"),
                Piece.Location.Equals(FVector(Width * (1.5f + Index * 2.f), 0.f, 0.f), 0.01f));
        }
    }
    {
        UBoxComponent* Nav = Cast<UBoxComponent>(Component(TEXT("Nav_Box")));
        UBoxComponent* Later = Cast<UBoxComponent>(Component(TEXT("LaterTaskTrigger")));
        TestTrue(TEXT("the far side is where the level has it"), Component(TEXT("OutSideR"))->GetRelativeLocation().Equals(FarSide, 0.01f));
        TestTrue(TEXT("the navigation box is where and as long as the level has it"),
            Nav->GetRelativeLocation().Equals(NavAt, 0.01f) && Nav->GetUnscaledBoxExtent().Equals(NavExtent, 0.01f));
        TestTrue(TEXT("the trigger box is where and as long as the level has it"),
            Later->GetRelativeLocation().Equals(LaterAt, 0.01f) && Later->GetUnscaledBoxExtent().Equals(LaterExtent, 0.01f));
        TestTrue(TEXT("the inside collision spans the gate's five units"),
            FMath::IsNearlyEqual(Component(TEXT("Col_Inside"))->GetRelativeScale3D().X, 5.f));
        TestTrue(TEXT("the far side's collision is at the far side"),
            Component(TEXT("Col_OutsideR"))->GetRelativeLocation().Equals(FarSide, 0.01f));
    }

    // A longer wall, set in the Details panel: 7 more units are a piece of five and one of two.
    const auto SetLength = [&](int32 Value)
    {
        FEditPropertyChain Chain;
        Chain.AddHead(Length);
        // The chain form, as the Details panel calls it: it tells the delegates before the value changes.
        static_cast<UObject*>(Gate)->PreEditChange(Chain);
        Length->SetPropertyValue_InContainer(Gate, Value);
        FPropertyChangedEvent Event(Length, EPropertyChangeType::ValueSet);
        Gate->PostEditChangeProperty(Event);
    };
    const float Left = FindFProperty<FFloatProperty>(Gate->GetClass(), TEXT("OutSideLWidth"))->GetPropertyValue_InContainer(Gate);
    const float Right = FindFProperty<FFloatProperty>(Gate->GetClass(), TEXT("OutSideRWidth"))->GetPropertyValue_InContainer(Gate);
    AddInfo(FString::Printf(TEXT("WallWidth %g, OutSideLWidth %g, OutSideRWidth %g"), Width, Left, Right));
    SetLength(7);
    {
        TArray<FString> Tags;
        TArray<float> Places;
        for (UStaticMeshComponent* Piece : P4GateWallPreviewPieces(Gate))
        {
            Tags.Add(Describe(Piece).Tags);
            Places.Add(Piece->GetRelativeLocation().X);
            TestTrue(TEXT("a new piece hangs on the wall's first unit"),
                Piece->GetAttachParent() && Piece->GetAttachParent()->GetName().Contains(TEXT("Mesh_Inside_00")));
            TestTrue(TEXT("a new piece has the game's mesh"), Piece->GetStaticMesh() != nullptr);
            TestTrue(TEXT("a new piece is not saved"), Piece->HasAnyFlags(RF_Transient));
        }
        TestEqual(TEXT("seven more units are a piece of five and one of two"), FString::Join(Tags, TEXT(";")),
            FString(TEXT("Wall_0_0,Width_5,AddWall;Wall_1_0,Width_2,AddWall")));
        TestTrue(TEXT("the pieces are where the game puts them"), Places.Num() == 2 &&
            FMath::IsNearlyEqual(Places[0], Width * 3.f) && FMath::IsNearlyEqual(Places[1], Width * 6.5f));
        UBoxComponent* Nav = Cast<UBoxComponent>(Component(TEXT("Nav_Box")));
        const float Middle = Left * 0.5f + Width * 4.f;
        TestTrue(TEXT("the far side moves out by three units"),
            FMath::IsNearlyEqual(Component(TEXT("OutSideR"))->GetRelativeLocation().X, Right * 0.5f + Left * 0.5f + Width * 8.f));
        TestTrue(TEXT("the navigation box covers the longer wall"),
            FMath::IsNearlyEqual(Nav->GetRelativeLocation().X, Middle) &&
                FMath::IsNearlyEqual(Nav->GetUnscaledBoxExtent().X, Width * 3.5f + 50.f));
        TestTrue(TEXT("the inside collision spans eight units"),
            FMath::IsNearlyEqual(Component(TEXT("Col_Inside"))->GetRelativeScale3D().X, 8.f));
        // A save or an export sees the map's own values, and the preview's again after.
        {
            FP4SuspendNativePreviews Out;
            TestEqual(TEXT("no preview piece while the previews are out"), P4GateWallPreviewPieces(Gate).Num(), 0);
            Same(SavedPieces(), Level, TEXT("suspending the preview restores the level's saved pieces"));
            TestTrue(TEXT("the map's far side while the previews are out"),
                Component(TEXT("OutSideR"))->GetRelativeLocation().Equals(FarSide, 0.01f));
            TestTrue(TEXT("the map's navigation box while the previews are out"),
                Cast<UBoxComponent>(Component(TEXT("Nav_Box")))->GetUnscaledBoxExtent().Equals(NavExtent, 0.01f));
            TestTrue(TEXT("the map's inside collision while the previews are out"),
                Component(TEXT("Col_Inside"))->GetRelativeScale3D().Equals(InsideScale, 0.001f));
        }
        TestEqual(TEXT("the pieces come back with the previews"), P4GateWallPreviewPieces(Gate).Num(), 2);
    }
    // The gate's length as it was: the saved pieces again.
    SetLength(Saved);
    TArray<FPiece> Restored;
    for (UStaticMeshComponent* Piece : P4GateWallPreviewPieces(Gate))
        Restored.Add(Describe(Piece));
    Same(Sorted(MoveTemp(Restored)), Drawn, TEXT("the saved length draws the original preview again"));
    return true;
}
#endif
