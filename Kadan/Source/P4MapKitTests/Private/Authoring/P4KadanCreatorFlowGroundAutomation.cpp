#include "P4KadanCreatorFlowStageList.h"
#include "P4KadanCreatorFlowShared.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// GroundBefore and Ground: ground types applied to supported, empty and invalid level actors.
class FGroundStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

private:
    bool GroundFixture();

    bool GroundSelection(bool Mixed);

    bool GroundCollisionApplied();

    FFlowAssets Assets{Flow};
    TWeakObjectPtr<AStaticMeshActor> GroundActor;
    TWeakObjectPtr<AActor> GroundSkippedActor;
    TWeakObjectPtr<AStaticMeshActor> GroundFailedActor;
};
} // namespace

void FGroundStage::AddSteps()
{
    Flow.Add(
        TEXT("Prepare independent scenery and mixed selection fixtures"), [this]() { return GroundFixture(); }, 180);
    Flow.Add(
        TEXT("Open the actual Kadan panel"), [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Use the narrow panel"), [this]() { return Flow.NarrowPanel(); });
    Flow.Add(TEXT("Switch to the actual Asset page"), [this]() { return Flow.Click(TEXT("Asset")); });
    Flow.Add(TEXT("Discover ground types through the actual refresh control"),
        [this]() { return Flow.Click(TEXT("Refresh imported ground types")); });
    const FString Apply = Flow.Stage == TEXT("GroundBefore") ? TEXT("Add terrain collision and use this ground type")
                                                             : TEXT("Apply ground type");
    Flow.Add(TEXT("Apply with only a Content Browser selection"), [this, Apply]() { return Flow.Click(Apply); });
    Flow.Add(
        TEXT("Capture Content Browser-only selection"), [this]() { return Flow.Screenshot(TEXT("browser-only")); });
    Flow.Add(TEXT("Select the supported level actor"), [this]() { return GroundSelection(false); });
    Flow.Add(TEXT("Apply through the actual actor control"), [this, Apply]() { return Flow.Click(Apply); });
    Flow.Add(TEXT("Inspect the resulting hidden terrain collision"), [this]() { return GroundCollisionApplied(); });
    Flow.Add(TEXT("Capture the supported actor result"), [this]() { return Flow.Screenshot(TEXT("supported")); });
    Flow.Add(TEXT("Select supported, empty and invalid actors together"), [this]() { return GroundSelection(true); });
    Flow.Add(TEXT("Apply to the mixed level selection"), [this, Apply]() { return Flow.Click(Apply); });
    if (Flow.Stage == TEXT("Ground"))
    {
        Flow.Add(TEXT("Mixed selection retains all outcomes and exact actor names"),
            [this]()
            {
                return Flow.Expect(Flow.TaggedText(TEXT("KadanGroundResult"))
                                       .Contains(TEXT("Applied to 1 actor. Skipped 1 actor. Failed for 1 actor.")) &&
                               Flow.VisibleText(TEXT("Empty actor was skipped."), true) &&
                               Flow.VisibleText(TEXT("Mesh without slots could not change."), true) &&
                               Flow.VisibleText(TEXT("Ground platform now uses"), true),
                           TEXT("The mixed selection lost an actor outcome")) &&
                    GroundCollisionApplied();
            });
    }
    Flow.Add(TEXT("Capture the mixed selection result"), [this]() { return Flow.Screenshot(TEXT("mixed")); });
    Flow.Add(TEXT("Reveal collision status and Unreal controls"),
        [this]()
        {
            const auto Panel = Flow.Tab();
            return Flow.Stage == TEXT("GroundBefore") ||
                (Panel && Flow.Prepare(Text(Panel->GetContent(), TEXT("Open mesh collision controls"))));
        });
    Flow.Add(TEXT("Capture separate collision status and display guidance"),
        [this]() { return Flow.Screenshot(TEXT("collision")); });
}

bool FGroundStage::GroundFixture()
{
    if (!Assets.MapProgressFixture(false))
    {
        return false;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    GroundActor = FindObject<AStaticMeshActor>(World->PersistentLevel, TEXT("KadanProgressCube"));
    if (!Flow.Expect(GroundActor.IsValid(), TEXT("The owned supported actor is unavailable")))
    {
        return false;
    }
    GroundActor->SetActorLabel(TEXT("Ground platform"));
    GroundSkippedActor = World->SpawnActor<AActor>();
    GroundSkippedActor->SetActorLabel(TEXT("Empty actor"));
    UStaticMesh* Invalid = DuplicateObject<UStaticMesh>(GroundActor->GetStaticMeshComponent()->GetStaticMesh(),
        CreatePackage(*(Flow.BNames.PackageRoot / TEXT("GroundWithoutSlots"))), TEXT("GroundWithoutSlots"));
    Invalid->StaticMaterials.Empty();
    GroundFailedActor = World->SpawnActor<AStaticMeshActor>();
    GroundFailedActor->SetActorLabel(TEXT("Mesh without slots"));
    GroundFailedActor->GetStaticMeshComponent()->SetStaticMesh(Invalid);
    GEditor->SelectNone(false, true);
    return Assets.SelectFixture(Flow.BPackage);
}

bool FGroundStage::GroundSelection(bool Mixed)
{
    GEditor->SelectNone(false, true);
    GEditor->SelectActor(GroundActor.Get(), true, true);
    if (Mixed)
    {
        GEditor->SelectActor(GroundSkippedActor.Get(), true, true);
        GEditor->SelectActor(GroundFailedActor.Get(), true, true);
    }
    return true;
}

bool FGroundStage::GroundCollisionApplied()
{
    TInlineComponentArray<UStaticMeshComponent*> Parts(GroundActor.Get());
    int32 Terrain = 0;
    for (auto* Part : Parts)
    {
        TArray<uint32> Tags;
        if (P4ReadGroundTags(Part, Tags) && Tags.Num())
        {
            ++Terrain;
            if (!Flow.Expect(!Part->IsVisible() && Part->bHiddenInGame &&
                        Part->GetCollisionEnabled() != ECollisionEnabled::NoCollision &&
                        Part->CanEverAffectNavigation(),
                    TEXT("Applied terrain collision is visible or has no collision or navigation")))
            {
                return false;
            }
        }
    }
    return Flow.Expect(Terrain == 1, TEXT("Applying the ground type did not create exactly one hidden tagged surface"));
}

TUniquePtr<FFlowStage> MakeGroundStage(FFlow& Flow)
{
    return MakeUnique<FGroundStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif
