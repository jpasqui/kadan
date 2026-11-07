// A picture of a graph, drawn by the graph editor's own widget off screen.

#include "P4BlueprintImport.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GraphEditor.h"
#include "SGraphPanel.h"
#include "ImageUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"

namespace
{
	TSharedPtr<SWidget> FindWidget(const TSharedRef<SWidget>& Root, FName Type)
	{
		if (Root->GetType() == Type)
		{
			return Root;
		}
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			TSharedPtr<SWidget> Found = FindWidget(Children->GetChildAt(Index), Type);
			if (Found.IsValid())
			{
				return Found;
			}
		}
		return nullptr;
	}
}

bool UP4BlueprintImport::RenderGraph(const FString& BlueprintPath, const FString& GraphName, const FString& File, int32 Width, int32 Height)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Blueprint)
	{
		return false;
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	UEdGraph* Graph = nullptr;
	for (UEdGraph* Each : Graphs)
	{
		if (Each && Each->GetName() == GraphName)
		{
			Graph = Each;
		}
	}
	if (!Graph)
	{
		return false;
	}
	// Editable, as in the editor: a graph widget that is not editable draws "READ-ONLY" over the picture.
	TSharedRef<SGraphEditor> Editor = SNew(SGraphEditor).GraphToEdit(Graph);
	FWidgetRenderer Renderer(true, false);
	const FVector2D Size(Width, Height);
	UTextureRenderTarget2D* Target = FWidgetRenderer::CreateTargetFor(Size, TF_Bilinear, true);
	if (!Target)
	{
		return false;
	}
	// An off-screen draw does not run the panel's own update, so the node widgets are made here, and
	// the view is set from the nodes' positions (the editor's "zoom to fit" also waits for an update).
	Renderer.DrawWidget(Target, Editor, Size, 0.1f);
	FlushRenderingCommands();
	TSharedPtr<SWidget> Panel = FindWidget(Editor, FName(TEXT("SGraphPanel")));
	if (Panel.IsValid())
	{
		StaticCastSharedPtr<SGraphPanel>(Panel)->Update();
	}
	FVector2D Low(MAX_FLT, MAX_FLT), High(-MAX_FLT, -MAX_FLT);
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			Low = FVector2D(FMath::Min(Low.X, (float)Node->NodePosX), FMath::Min(Low.Y, (float)Node->NodePosY));
			High = FVector2D(FMath::Max(High.X, Node->NodePosX + 420.f), FMath::Max(High.Y, Node->NodePosY + 320.f));
		}
	}
	if (Low.X > High.X)
	{
		Low = FVector2D::ZeroVector;
		High = Size;
	}
	const float Zoom = FMath::Clamp(FMath::Min(Size.X / (High.X - Low.X + 200.f), Size.Y / (High.Y - Low.Y + 200.f)), 0.1f, 1.f);
	Editor->SetViewLocation(Low - FVector2D(100.f, 100.f), Zoom);
	for (int32 Pass = 0; Pass < 6; ++Pass)
	{
		Renderer.DrawWidget(Target, Editor, Size, 0.5f);
		FlushRenderingCommands();
	}
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	TArray<FColor> Pixels;
	if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Width * Height)
	{
		return false;
	}
	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
	}
	TArray<uint8> Png;
	FImageUtils::CompressImageArray(Width, Height, Pixels, Png);
	return FFileHelper::SaveArrayToFile(Png, *File);
}
