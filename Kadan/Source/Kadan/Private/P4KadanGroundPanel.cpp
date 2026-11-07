#include "P4KadanGroundPanel.h"
#include "P4KadanReadOnly.h"
#include "P4PlacementExport.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "LandscapeProxy.h"
#include "PhysicsEngine/BodySetup.h"
#include "Styling/CoreStyle.h"
#include "Toolkits/AssetEditorManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
TArray<AActor*> SelectedActors()
{
    TArray<AActor*> Result;
    if (GEditor)
    {
        for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
        {
            if (AActor* Actor = Cast<AActor>(*It))
            {
                Result.Add(Actor);
            }
        }
    }
    return Result;
}

bool HasSurface(AActor* Actor)
{
    if (Actor->IsA<ALandscapeProxy>())
    {
        return true;
    }
    TInlineComponentArray<UStaticMeshComponent*> Parts(Actor);
    return Parts.ContainsByPredicate([](const UStaticMeshComponent* Part) { return Part && Part->GetStaticMesh(); });
}

TArray<UStaticMesh*> SelectedMeshes()
{
    TArray<UStaticMesh*> Meshes;
    for (AActor* Actor : SelectedActors())
    {
        TInlineComponentArray<UStaticMeshComponent*> Parts(Actor);
        for (UStaticMeshComponent* Part : Parts)
        {
            if (Part && Part->GetStaticMesh())
            {
                Meshes.AddUnique(Part->GetStaticMesh());
            }
        }
    }
    return Meshes;
}

FString CollisionStatus()
{
    const auto Actors = SelectedActors();
    if (Actors.Num() == 0)
    {
        return TEXT("No level actors are selected. Content Browser selection does not apply a ground type.");
    }
    int32 Shapes = 0;
    int32 Triangles = 0;
    int32 Empty = 0;
    for (UStaticMesh* Mesh : SelectedMeshes())
    {
        if (Mesh->BodySetup && Mesh->BodySetup->GetCollisionTraceFlag() == CTF_UseComplexAsSimple)
        {
            ++Triangles;
        }
        else if (Mesh->BodySetup && Mesh->BodySetup->AggGeom.GetElementCount() > 0)
        {
            ++Shapes;
        }
        else
        {
            ++Empty;
        }
    }
    return FString::Printf(
        TEXT(
            "The selection contains %d level actor%s. Simple collision is configured on %d mesh%s and triangle collision on %d mesh%s. %d mesh%s %s no configured collision surface. Check component collision and navigation in Unreal before exporting."),
        Actors.Num(), Actors.Num() == 1 ? TEXT("") : TEXT("s"), Shapes, Shapes == 1 ? TEXT("") : TEXT("es"), Triangles,
        Triangles == 1 ? TEXT("") : TEXT("es"), Empty, Empty == 1 ? TEXT("") : TEXT("es"),
        Empty == 1 ? TEXT("has") : TEXT("have"));
}

class SKadanGroundPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SKadanGroundPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&)
    {
        SetTag(FName(TEXT("KadanGroundPanel")));
        ChildSlot[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Heading(TEXT("Ground types"))] +
            SVerticalBox::Slot().AutoHeight()[Message(TEXT(
                "Select scenery actors in the level, then choose a ground type. Kadan sets the surface type and adds hidden terrain collision. It uses the mesh's collision shapes or its triangles when needed. Save the meshes and their level before exporting."))] +
            SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Left)
                .Padding(0, 4)[Button(TEXT("Refresh imported ground types"),
                    [this]()
                    {
                        Types.Empty();
                        for (const auto& Type : P4ImportedGroundTypes())
                        {
                            Types.Add(MakeShared<FP4GroundType>(Type));
                        }
                        Choice = Types.Num() ? Types[0] : nullptr;
                        Picker->RefreshOptions();
                        Picker->SetSelectedItem(Choice);
                        Status = Types.Num()
                            ? TEXT("Choose a ground type for the selected level actors.")
                            : TEXT("Import game scenery with collision to make its ground types available.");
                        Rows->ClearChildren();
                    })] +
            SVerticalBox::Slot().AutoHeight().Padding(0, 4)[SAssignNew(Picker, SComboBox<TSharedPtr<FP4GroundType>>)
                    .Tag(TEXT("KadanGroundPicker"))
                    .OptionsSource(&Types)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FP4GroundType> Type)
                        { return SNew(STextBlock).Text(FText::FromString(Type->Name)); })
                    .OnSelectionChanged_Lambda(
                        [this](TSharedPtr<FP4GroundType> Type, ESelectInfo::Type) { Choice = Type; })[SNew(STextBlock)
                            .Text_Lambda([this]()
                                { return FText::FromString(Choice ? Choice->Name : TEXT("Choose a ground type")); })]] +
            SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Left)
                .Padding(0, 4)[Button(TEXT("Apply ground type"),
                    [this]()
                    {
                        Rows->ClearChildren();
                        if (!Choice)
                        {
                            Status = TEXT("Refresh the imported ground types and choose a surface first.");
                            return;
                        }
                        const auto Actors = SelectedActors();
                        if (!Actors.Num())
                        {
                            Status = TEXT(
                                "Select scenery actors in the level first. Content Browser selection does not apply a ground type.");
                            return;
                        }
                        const auto Result = P4KadanApplyGroundType(Actors, *Choice);
                        Status = Result.Summary();
                        for (const FString& Row : Result.Rows)
                        {
                            Rows->AddSlot().AutoHeight()[Message(Row)];
                        }
                    })] +
            SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                    .Tag(TEXT("KadanGroundResult"))
                    .AutoWrapText(true)
                    .Text_Lambda([this]() { return FText::FromString(Status); })] +
            SVerticalBox::Slot().AutoHeight()[SAssignNew(Rows, SVerticalBox)] +
            SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Heading(TEXT("Collision"))] +
            SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                    .Tag(TEXT("KadanCollisionStatus"))
                    .AutoWrapText(true)
                    .Text_Lambda([]() { return FText::FromString(CollisionStatus()); })] +
            SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Left)
                .Padding(0, 4)[Button(
                    TEXT("Open mesh collision controls"),
                    []()
                    {
                        for (UStaticMesh* Mesh : SelectedMeshes())
                        {
                            FAssetEditorManager::Get().OpenEditorForAsset(Mesh);
                        }
                    },
                    TAttribute<bool>::Create(
                        TAttribute<bool>::FGetter::CreateLambda([]() { return SelectedMeshes().Num() == 1; })))] +
            SVerticalBox::Slot()
                .AutoHeight()[Message(TEXT("Select one scenery mesh in the level to open its collision controls."))] +
            SVerticalBox::Slot().AutoHeight()[Message(TEXT(
                "In the mesh editor, use the Collision menu and the Collision section in Details to inspect the surface. In the level viewport, use Show and Collision to display collision and press P to display navigation. Do not place visible collision geometry as a display aid. Editor collision does not establish Pikmin path coverage in the game."))]];
    }

private:
    TArray<TSharedPtr<FP4GroundType>> Types;
    TSharedPtr<FP4GroundType> Choice;
    TSharedPtr<SComboBox<TSharedPtr<FP4GroundType>>> Picker;
    TSharedPtr<SVerticalBox> Rows;
    FString Status;

    TSharedRef<SWidget> Heading(const FString& Value)
    {
        return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12));
    }

    TSharedRef<SWidget> Message(const FString& Value)
    {
        return SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Value));
    }

    TSharedRef<SWidget> Button(const FString& Caption, TFunction<void()> Action, TAttribute<bool> Enabled = true)
    {
        return SNew(SButton)
            .ButtonStyle(FEditorStyle::Get(), "Button")
            .ButtonColorAndOpacity(FLinearColor(0.12f, 0.14f, 0.18f))
            .IsEnabled(Enabled)
            .TextStyle(FEditorStyle::Get(), "NormalText")
            .ForegroundColor(FLinearColor::White)
            .ContentPadding(FMargin(8, 4))
            .Text(FText::FromString(Caption))
            .OnClicked_Lambda(
                [Action]()
                {
                    Action();
                    return FReply::Handled();
                });
    }
};
} //namespace

FString FP4KadanGroundResult::Summary() const
{
    return FString::Printf(
        TEXT(
            "Applied to %d actor%s. Skipped %d actor%s. Failed for %d actor%s. Save the changed meshes and levels, then review the export again."),
        Applied, Applied == 1 ? TEXT("") : TEXT("s"), Skipped, Skipped == 1 ? TEXT("") : TEXT("s"), Failed,
        Failed == 1 ? TEXT("") : TEXT("s"));
}

FP4KadanGroundResult P4KadanApplyGroundType(const TArray<AActor*>& Actors, const FP4GroundType& Type)
{
    FP4KadanGroundResult Result;
    for (AActor* Actor : Actors)
    {
        if (!IsValid(Actor))
        {
            ++Result.Skipped;
            Result.Rows.Add(TEXT("An unavailable actor was skipped."));
            continue;
        }
        const FString Name = Actor->GetActorLabel();
        if (P4Kadan::GameActorOf(Actor) || P4IsGameplayActor(Actor) || !HasSurface(Actor))
        {
            ++Result.Skipped;
            Result.Rows.Add(
                Name + TEXT(" was skipped. Choose scenery added to the map with a static mesh or landscape surface."));
            continue;
        }
        FString Error;
        if (P4SetCreatorGroundType(Actor, Type, Error))
        {
            ++Result.Applied;
            Result.Rows.Add(Name + TEXT(" now uses ") + Type.Name + TEXT(" for its ground type."));
        }
        else
        {
            ++Result.Failed;
            Result.Rows.Add(Name + TEXT(" could not change. ") + Error);
        }
    }
    return Result;
}

TSharedRef<SWidget> P4KadanGroundPanel()
{
    return SNew(SKadanGroundPanel);
}
