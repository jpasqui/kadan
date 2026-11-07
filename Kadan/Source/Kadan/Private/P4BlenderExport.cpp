#include "P4BlenderExport.h"
#include "P4KadanSettings.h"
#include "P4BlenderData.h"
#include "P4CreatorSource.h"
#include "P4KadanImport.h"
#include "P4KadanSourceCatalog.h"
#include "P4KadanProjectSetup.h"
#include "ActorEditorUtils.h"
#include "AssetExportTask.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Exporters/FbxExportOption.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/GCObjectScopeGuard.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UnrealExporter.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY_STATIC(LogKadanBlender, Log, All);

namespace
{
const int64 Version = 1;
// Properties of an expression that are not its settings (its place in the editor is kept apart).
const TCHAR* const Skip[] = {TEXT("MaterialExpressionEditorX"), TEXT("MaterialExpressionEditorY"),
    TEXT("MaterialExpressionGuid"), TEXT("Material"), TEXT("Function"), TEXT("ExpressionGUID"), TEXT("ParameterId"),
    TEXT("Outputs"), TEXT("bCollapsed"), TEXT("bRealtimePreview"), TEXT("bNeedToUpdatePreview"),
    TEXT("bIsParameterExpression"), TEXT("GraphNode"), TEXT("EditorX"), TEXT("EditorY")};
// Properties of the material itself that the add-on brings back, by their Python names.
const TCHAR* const MaterialProperties[] = {TEXT("blend_mode"), TEXT("shading_model"), TEXT("two_sided"),
    TEXT("opacity_mask_clip_value"), TEXT("tangent_space_normal"), TEXT("translucency_lighting_mode"),
    TEXT("screen_space_reflections"), TEXT("dithered_lod_transition"), TEXT("material_domain")};
const TCHAR* const Outputs[] = {TEXT("EmissiveColor"), TEXT("BaseColor"), TEXT("Roughness"), TEXT("Metallic"),
    TEXT("Specular"), TEXT("Normal"), TEXT("Opacity"), TEXT("OpacityMask"), TEXT("AmbientOcclusion"),
    TEXT("SubsurfaceColor"), TEXT("Refraction"), TEXT("WorldPositionOffset"), TEXT("Anisotropy"), TEXT("Tangent"),
    TEXT("PixelDepthOffset"), TEXT("ClearCoat"), TEXT("ClearCoatRoughness")};

bool In(const TCHAR* const* List, int32 Count, const FString& Name)
{
    for (int32 I = 0; I < Count; ++I)
        if (Name.Equals(List[I], ESearchCase::CaseSensitive))
            return true;
    return false;
}

// path.split('.')[0]
FString PackagePart(const FString& Path)
{
    int32 Dot;
    return Path.FindChar('.', Dot) ? Path.Left(Dot) : Path;
}

FString ShortName(const FString& Path)
{
    int32 Slash;
    return Path.FindLastChar('/', Slash) ? Path.Mid(Slash + 1) : Path;
}

bool Before(const FString& A, const FString& B)
{
    return A.Compare(B, ESearchCase::CaseSensitive) < 0;
}

FP4PyJson Vector3(const FVector& V)
{
    FP4PyJson Out = FP4PyJson::List();
    Out.Add(FP4PyJson::Number(V.X));
    Out.Add(FP4PyJson::Number(V.Y));
    Out.Add(FP4PyJson::Number(V.Z));
    return Out;
}

FP4PyJson TextOrNull(UObject* Object)
{
    return Object ? FP4PyJson::Text(PackagePart(Object->GetPathName())) : FP4PyJson::Null();
}

// A file name for an asset, unique in its folder.
FString FileName(const FString& Path, TMap<FString, FString>& Used, const TCHAR* Extension)
{
    FString Stem = ShortName(PackagePart(Path));
    for (TCHAR& C : Stem)
        if (!((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_' || C == '.' ||
                C == '-'))
            C = '_';
    FString Name = Stem;
    for (int32 N = 1; Used.Contains(Name.ToLower()) && Used[Name.ToLower()] != Path;)
        Name = FString::Printf(TEXT("%s_%d"), *Stem, ++N);
    Used.Add(Name.ToLower(), Path);
    return Name + Extension;
}

bool RunExport(UObject* Object, const FString& Filename, UObject* Options = nullptr)
{
    UAssetExportTask* Task = NewObject<UAssetExportTask>();
    FGCObjectScopeGuard Guard(Task);
    Task->Object = Object;
    Task->Filename = Filename;
    Task->bAutomated = true;
    Task->bPrompt = false;
    Task->bReplaceIdentical = true;
    Task->Options = Options;
    return UExporter::RunAssetExportTask(Task);
}

bool ExportMesh(UStaticMesh* Mesh, const FString& Target)
{
    UFbxExportOption* Options = NewObject<UFbxExportOption>();
    FGCObjectScopeGuard Guard(Options);
    Options->Collision = false;
    Options->LevelOfDetail = false;
    Options->VertexColor = true;
    return RunExport(Mesh, Target, Options);
}

// A reflected function of the object that returns an int32 and takes nothing (as a script calls it).
int32 CallInt(UObject* Object, const TCHAR* Name)
{
    UFunction* Function = Object ? Object->FindFunction(FName(Name)) : nullptr;
    if (!Function || Function->ParmsSize != sizeof(int32))
        return 0;
    int32 Value = 0;
    Object->ProcessEvent(Function, &Value);
    return Value;
}

// The texture as PNG, through Unreal's TGA exporter. Null when Unreal has no source image for it.
FP4PyJson ExportTexture(UTexture2D* Texture, const FString& Target)
{
    const FString Tga = Target.LeftChop(4) + TEXT(".tga");
    FP4PyJson Facts;
    FString Error;
    TArray<uint8> Data, Png;
    if (RunExport(Texture, Tga) && FPaths::FileExists(Tga))
    {
        if (FFileHelper::LoadFileToArray(Data, *Tga) && P4Blender::TgaToPng(Data, Png, Error) &&
            FFileHelper::SaveArrayToFile(Png, *Target))
        {
            Facts = FP4PyJson::Dict();
            Facts.Set(TEXT("srgb"), FP4PyJson::Boolean(Texture->SRGB));
            Facts.Set(TEXT("normal"), FP4PyJson::Boolean(Texture->CompressionSettings == TC_Normalmap));
            Facts.Set(TEXT("width"), FP4PyJson::Integer(CallInt(Texture, TEXT("Blueprint_GetSizeX"))));
            Facts.Set(TEXT("height"), FP4PyJson::Integer(CallInt(Texture, TEXT("Blueprint_GetSizeY"))));
        }
        else
            UE_LOG(LogKadanBlender, Warning, TEXT("The Blender export skips the texture %s (%s)."), *Texture->GetName(),
                Error.IsEmpty() ? TEXT("its file could not be written") : *Error);
    }
    if (FPaths::FileExists(Tga))
        IFileManager::Get().Delete(*Tga);
    return Facts;
}

// Unreal's text export (T3D) of a material, as the asset export task makes it for a .t3d file.
bool MaterialText(UMaterial* Material, FString& Text)
{
    UExporter* Exporter = UExporter::FindExporter(Material, TEXT("t3d"));
    if (!Exporter)
        return false;
    FGCObjectScopeGuard Guard(Exporter);
    Exporter->bSelectedOnly = false;
    FStringOutputDevice Buffer;
    const FExportObjectInnerContext Context{TArray<UObject*>()};
    UExporter::ExportToOutputDevice(
        &Context, Material, Exporter, Buffer, TEXT("t3d"), 0, PPF_ExportsNotFullyQualified, false);
    Text = Buffer;
    return !Text.IsEmpty();
}

float CollectionValue(UObject* Object)
{
    const UMaterialExpressionCollectionParameter* Expression = Cast<UMaterialExpressionCollectionParameter>(Object);
    if (!Expression || !Expression->Collection)
        return 1.0f;
    for (const FCollectionScalarParameter& Parameter : Expression->Collection->ScalarParameters)
        if (Parameter.ParameterName.ToString() == Expression->ParameterName.ToString())
            return Parameter.DefaultValue;
    return 1.0f;
}

// A material's node graph: material_t3d's, plus for each node 'py' (its properties under their Python
// names), 'pos' (its place in the editor) and 'unread_links' (connections the text export has in a
// form material_t3d does not read), and the outputs material_t3d leaves out.
bool GraphOf(UMaterial* Material, TSet<FString>* Notes, FP4PyJson& Graph, FString& Error)
{
    FString Text;
    if (!MaterialText(Material, Text))
    {
        Error = FString::Printf(TEXT("Unreal could not export %s as text"), *Material->GetName());
        return false;
    }
    Graph = P4Blender::ParseMaterialT3D(Text);
    const TMap<FString, TArray<FString>> Blocks = P4Blender::TextBlocks(Text);
    const FString Base = Material->GetPathName();
    static const FRegexPattern LinePattern(TEXT("^(\\w+)(?:\\((\\d+)\\))?=(.*)$"));
    static const FRegexPattern OutputPattern(TEXT("^(\\w+)=(\\(.*\\))$"));
    static const TArray<FString> NoLines;
    FP4PyJson& Nodes = *Graph.Find(TEXT("nodes"));
    for (auto& Entry : Nodes.Fields)
    {
        const FString& Name = Entry.first;
        FP4PyJson& Node = Entry.second;
        const TArray<FString>* Found = Blocks.Find(Name);
        const TArray<FString>& Lines = Found ? *Found : NoLines;
        UObject* Expression = StaticFindObject(UObject::StaticClass(), nullptr, *(Base + TEXT(":") + Name));
        const FString Type = Node.Find(TEXT("type"))->String;
        const FP4PyJson& Inputs = *Node.Find(TEXT("inputs"));
        FP4PyJson Py = FP4PyJson::Dict(), Unread = FP4PyJson::List();
        int64 Pos[2] = {0, 0};
        TSet<FString> Seen;
        for (const FString& Line : Lines)
        {
            FRegexMatcher M(LinePattern, Line);
            if (!M.FindNext())
                continue;
            const FString Key = M.GetCaptureGroup(1), Value = M.GetCaptureGroup(3);
            const FString Pin = M.GetCaptureGroupBeginning(2) == INDEX_NONE ? Key : Key + M.GetCaptureGroup(2);
            double Coordinate;
            if (Key == TEXT("MaterialExpressionEditorX") && P4Blender::PythonParseFloat(Value, Coordinate))
                Pos[0] = (int64)Coordinate;
            else if (Key == TEXT("MaterialExpressionEditorY") && P4Blender::PythonParseFloat(Value, Coordinate))
                Pos[1] = (int64)Coordinate;
            if (Value.Contains(TEXT("Expression="), ESearchCase::CaseSensitive))
            {
                bool Listed = Inputs.Find(Pin) != nullptr;
                for (const FP4PyJson& Item : Unread.Items)
                    Listed |= Item.String.Equals(Pin, ESearchCase::CaseSensitive);
                if (!Listed)
                    Unread.Add(FP4PyJson::Text(Pin));
                continue;
            }
            if (In(Skip, UE_ARRAY_COUNT(Skip), Key) || Seen.Contains(Key))
                continue;
            Seen.Add(Key);
            const FString PyName = P4Blender::KitPythonName(Key);
            FP4PyJson Data;
            if (Expression)
                if (const FProperty* Property = P4Blender::EditorProperty(Expression->GetClass(), PyName))
                    Data = P4Blender::EncodeProperty(Property, Expression);
            if (Data.IsNull())
            {
                if (Notes && Key != TEXT("Desc"))
                    Notes->Add(FString::Printf(TEXT("The property %s of %s nodes is not exported."), *Key, *Type));
                continue;
            }
            Py.Set(PyName, MoveTemp(Data));
        }
        Node.Set(TEXT("py"), MoveTemp(Py));
        FP4PyJson Place = FP4PyJson::List();
        Place.Add(FP4PyJson::Integer(Pos[0]));
        Place.Add(FP4PyJson::Integer(Pos[1]));
        Node.Set(TEXT("pos"), MoveTemp(Place));
        if (!Unread.Items.empty())
            Node.Set(TEXT("unread_links"), MoveTemp(Unread));
        if (Type == TEXT("CollectionParameter"))
            Node.Set(TEXT("preview"), FP4PyJson::Number(CollectionValue(Expression)));
    }
    FP4PyJson& GraphOutputs = *Graph.Find(TEXT("outputs"));
    if (const TArray<FString>* MaterialLines = Blocks.Find(Material->GetName()))
    {
        for (const FString& Line : *MaterialLines)
        {
            FRegexMatcher M(OutputPattern, Line);
            if (!M.FindNext())
                continue;
            const FString Pin = M.GetCaptureGroup(1), Value = M.GetCaptureGroup(2);
            if (!In(Outputs, UE_ARRAY_COUNT(Outputs), Pin) || GraphOutputs.Find(Pin) ||
                !Value.Contains(TEXT("Expression="), ESearchCase::CaseSensitive))
                continue;
            FP4PyJson Link = P4Blender::ParseLink(Value);
            if (!Link.IsNull())
                GraphOutputs.Set(Pin, MoveTemp(Link));
        }
    }
    return true;
}

// One material as the add-on's JSON: the root material's graph and settings, and an instance's values.
bool MaterialData(UMaterialInterface* Material, TSet<FString>* Notes, FP4PyJson& Data, FString& Error)
{
    UMaterialInterface* Root = Material;
    TArray<UMaterialInstance*> Chain;
    while (UMaterialInstance* Instance = Cast<UMaterialInstance>(Root))
    {
        Chain.Add(Instance);
        Root = Instance->Parent;
    }
    UMaterial* RootMaterial = Cast<UMaterial>(Root);
    if (!RootMaterial)
    {
        Error = FString::Printf(TEXT("%s has no material at the root of its parents"), *Material->GetName());
        return false;
    }
    FP4PyJson Graph;
    if (!GraphOf(RootMaterial, Notes, Graph, Error))
        return false;
    UMaterialInstanceConstant* Constant = Cast<UMaterialInstanceConstant>(Material);
    if (Chain.Num() && !Constant)
    {
        Error = FString::Printf(TEXT("%s is not a material instance constant"), *Material->GetName());
        return false;
    }
    Data = FP4PyJson::Dict();
    Data.Set(TEXT("version"), FP4PyJson::Integer(Version));
    Data.Set(TEXT("name"), FP4PyJson::Text(Material->GetName()));
    Data.Set(TEXT("path"), FP4PyJson::Text(Material->GetPathName()));
    Data.Set(TEXT("root"), FP4PyJson::Text(RootMaterial->GetPathName()));
    Data.Set(
        TEXT("parent"), FP4PyJson::Text(Chain.Num() && Chain[0]->Parent ? Chain[0]->Parent->GetPathName() : FString()));
    Data.Set(TEXT("graph"), MoveTemp(Graph));
    FP4PyJson Settings = FP4PyJson::Dict();
    for (const TCHAR* Key : MaterialProperties)
    {
        FP4PyJson Value;
        if (const FProperty* Property = P4Blender::EditorProperty(RootMaterial->GetClass(), Key))
            Value = P4Blender::EncodeProperty(Property, RootMaterial);
        if (!Value.IsNull())
            Settings.Set(Key, MoveTemp(Value));
    }
    Data.Set(TEXT("material"), MoveTemp(Settings));
    Data.Set(TEXT("class"), FP4PyJson::Text(Material->GetClass()->GetName()));
    // Kadan's addition to the Python kit's form: the root is a game material (Kadan's record of the game's
    // source or of the original package). The add-on says an edited copy of it cannot ship.
    if (P4HasIntrinsicGameSource(RootMaterial) ||
        !RootMaterial->GetOutermost()->GetMetaData()->GetValue(RootMaterial, TEXT("P4OriginalRecord")).IsEmpty())
        Data.Set(TEXT("game"), FP4PyJson::Boolean(true));
    const FString Tag = RootMaterial->GetOutermost()->GetMetaData()->GetValue(RootMaterial, TEXT("TransMat"));
    if (!Tag.IsEmpty() && !Chain.Num())
        Data.Set(TEXT("transmat"), FP4PyJson::Text(Tag));
    if (Chain.Num())
    {
        FP4PyJson Scalars = FP4PyJson::Dict(), Vectors = FP4PyJson::Dict(), Textures = FP4PyJson::Dict();
        TArray<FName> Names;
        UMaterialEditingLibrary::GetScalarParameterNames(Material, Names);
        for (const FName& Name : Names)
            Scalars.Set(Name.ToString(),
                FP4PyJson::Number(UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(Constant, Name)));
        Names.Reset();
        UMaterialEditingLibrary::GetVectorParameterNames(Material, Names);
        for (const FName& Name : Names)
        {
            const FLinearColor C = UMaterialEditingLibrary::GetMaterialInstanceVectorParameterValue(Constant, Name);
            FP4PyJson Color = FP4PyJson::List();
            for (float Channel : {C.R, C.G, C.B, C.A})
                Color.Add(FP4PyJson::Number(Channel));
            Vectors.Set(Name.ToString(), MoveTemp(Color));
        }
        Names.Reset();
        UMaterialEditingLibrary::GetTextureParameterNames(Material, Names);
        for (const FName& Name : Names)
            if (UTexture* Texture = UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(Constant, Name))
                Textures.Set(Name.ToString(), FP4PyJson::Text(Texture->GetPathName()));
        // Kadan's addition to the Python kit's form: the instance's static switches (its own or its parents').
        FP4PyJson Switches = FP4PyJson::Dict();
        Names.Reset();
        UMaterialEditingLibrary::GetStaticSwitchParameterNames(Material, Names);
        for (const FName& Name : Names)
            Switches.Set(Name.ToString(),
                FP4PyJson::Boolean(
                    UMaterialEditingLibrary::GetMaterialInstanceStaticSwitchParameterValue(Constant, Name)));
        FP4PyJson Values = FP4PyJson::Dict();
        Values.Set(TEXT("scalars"), MoveTemp(Scalars));
        Values.Set(TEXT("vectors"), MoveTemp(Vectors));
        Values.Set(TEXT("textures"), MoveTemp(Textures));
        Values.Set(TEXT("switches"), MoveTemp(Switches));
        Data.Set(TEXT("parameters"), MoveTemp(Values));
    }
    return true;
}

// The texture assets a material's JSON names (its nodes' and its instance values').
void TexturesOf(const FP4PyJson& Data, TSet<FString>& Found)
{
    if (const FP4PyJson* Parameters = Data.Find(TEXT("parameters")))
        if (const FP4PyJson* Textures = Parameters->Find(TEXT("textures")))
            for (const auto& Pair : Textures->Fields)
                Found.Add(Pair.second.String);
    for (const auto& Pair : Data.Find(TEXT("graph"))->Find(TEXT("nodes"))->Fields)
        if (const FP4PyJson* Py = Pair.second.Find(TEXT("py")))
            if (const FP4PyJson* Value = Py->Find(TEXT("texture")))
                if (Value->Kind == FP4PyJson::EKind::List && Value->Items.size() >= 2 &&
                    Value->Items[0].String == TEXT("asset"))
                    Found.Add(Value->Items[1].String);
}

UObject* LoadAsset(const FString& Path)
{
    FString ObjectPath = Path;
    if (!ObjectPath.Contains(TEXT(".")))
        ObjectPath += TEXT(".") + FPackageName::GetShortName(ObjectPath);
    return StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath, nullptr, LOAD_NoWarn);
}

TArray<FString> SortedKeys(const TMap<FString, UMaterialInterface*>& Map)
{
    TArray<FString> Keys;
    Map.GetKeys(Keys);
    Keys.Sort(&Before);
    return Keys;
}

bool WriteText(const FString& Text, const FString& File)
{
    return FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void Notify(const FString& Message, bool Good)
{
    if (!FSlateApplication::IsInitialized() || !FApp::CanEverRender() || IsRunningCommandlet())
        return;
    FNotificationInfo Info(FText::FromString(Message));
    Info.ExpireDuration = 10.f;
    Info.bUseLargeFont = false;
    Info.WidthOverride = 520.f;
    TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
    if (Item.IsValid())
        Item->SetCompletionState(Good ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
}
} //namespace

TArray<FP4BlenderPart> P4BlenderParts(AActor* Actor)
{
    TArray<FP4BlenderPart> Parts;
    if (!IsValid(Actor))
        return Parts;
    if (AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor))
    {
        // A Static Mesh Actor is one row with the actor's transform and bounds, as the Python kit wrote it.
        UStaticMeshComponent* Component = MeshActor->GetStaticMeshComponent();
        if (!Component || !Component->GetStaticMesh())
            return Parts;
        FP4BlenderPart& Part = Parts.AddDefaulted_GetRef();
        Part.Component = Component;
        Part.Label = Actor->GetActorLabel();
        Part.Transform = Actor->GetActorTransform();
        Actor->GetActorBounds(false, Part.Origin, Part.Extent);
        return Parts;
    }
    // Any other actor (a Blueprint of the game's, for one) gives a row for each mesh component, and an
    // instanced one a row for each instance.
    TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
    for (UStaticMeshComponent* Component : Components)
    {
        UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
        if (!Mesh || Component->IsEditorOnly() || Component->IsVisualizationComponent())
            continue;
        if (UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component))
        {
            for (int32 Index = 0; Index < Instanced->GetInstanceCount(); ++Index)
            {
                FP4BlenderPart Part;
                Part.Component = Component;
                Part.Instance = Index;
                if (!Instanced->GetInstanceTransform(Index, Part.Transform, true))
                    continue;
                const FBox Box = Mesh->GetBoundingBox().TransformBy(Part.Transform);
                Part.Origin = Box.GetCenter();
                Part.Extent = Box.GetExtent();
                Part.Label = FString::Printf(TEXT("%s.%s.%d"), *Actor->GetActorLabel(), *Component->GetName(), Index);
                Parts.Add(MoveTemp(Part));
            }
            continue;
        }
        FP4BlenderPart& Part = Parts.AddDefaulted_GetRef();
        Part.Component = Component;
        Part.Transform = Component->GetComponentTransform();
        Part.Origin = Component->Bounds.Origin;
        Part.Extent = Component->Bounds.BoxExtent;
        Part.Label = Actor->GetActorLabel() + TEXT(".") + Component->GetName();
    }
    if (Parts.Num() == 1 && Parts[0].Instance == INDEX_NONE)
        Parts[0].Label = Actor->GetActorLabel();
    return Parts;
}

TArray<AActor*> P4BlenderSceneActors(UWorld* World, bool SelectedOnly)
{
    TArray<AActor*> Actors;
    if (!World)
        return Actors;
    if (SelectedOnly && GEditor)
    {
        for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
            if (AActor* Actor = Cast<AActor>(*It))
                if (!Actor->IsPendingKill() && Actor->GetWorld() == World && P4BlenderParts(Actor).Num())
                    Actors.Add(Actor);
        if (Actors.Num())
            return Actors;
    }
    for (TActorIterator<AActor> It(World, AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor->IsEditable() && Actor->IsListedInSceneOutliner() && !Actor->IsTemplate() &&
            !Actor->HasAnyFlags(RF_Transient) && !FActorEditorUtils::IsABuilderBrush(Actor) &&
            !Actor->IsA(AWorldSettings::StaticClass()) && P4BlenderParts(Actor).Num())
            Actors.Add(Actor);
    }
    return Actors;
}

namespace
{
// The scene of the actors' meshes and of Given materials (a materials-only scene has no actors).
bool ExportScene(UWorld* World, const FString& SceneFile, const TArray<AActor*>& Actors,
    const TArray<UMaterialInterface*>& Given, FP4BlenderScene& Out)
{
    const FString Folder = FPaths::GetPath(SceneFile);
    TArray<const TCHAR*> Subs = {TEXT("textures"), TEXT("materials")};
    if (Actors.Num())
        Subs.Insert(TEXT("meshes"), 0);
    for (const TCHAR* Sub : Subs)
        if (!IFileManager::Get().MakeDirectory(*FPaths::Combine(Folder, Sub), true))
        {
            Out.Error = FString::Printf(TEXT("The folder %s could not be made."), *FPaths::Combine(Folder, Sub));
            return false;
        }
    TSet<FString> Notes;
    FP4PyJson Scene = FP4PyJson::Dict();
    Scene.Set(TEXT("version"), FP4PyJson::Integer(Version));
    Scene.Set(TEXT("level"), FP4PyJson::Text(World ? PackagePart(World->GetPathName()) : FString()));
    Scene.Set(TEXT("unit"), FP4PyJson::Text(TEXT("cm")));
    FP4PyJson SceneActors = FP4PyJson::List(), SceneMeshes = FP4PyJson::Dict();
    FP4PyJson SceneMaterials = FP4PyJson::Dict(), SceneTextures = FP4PyJson::Dict();
    TMap<FString, FString> UsedMeshes, UsedTextures, UsedMaterials;
    TMap<FString, UMaterialInterface*> Materials;
    for (UMaterialInterface* Material : Given)
        if (Material)
            Materials.Add(PackagePart(Material->GetPathName()), Material);
    for (const FP4BlenderPart& Part :
        [&]
        {
            TArray<FP4BlenderPart> All;
            for (AActor* Actor : Actors)
                All.Append(P4BlenderParts(Actor));
            return All;
        }())
    {
        UStaticMeshComponent* Component = Part.Component;
        UStaticMesh* Mesh = Component->GetStaticMesh();
        const FString Path = PackagePart(Mesh->GetPathName());
        if (!SceneMeshes.Find(Path))
        {
            const FString File = TEXT("meshes/") + FileName(Path, UsedMeshes, TEXT(".fbx"));
            UE_LOG(LogKadanBlender, Log, TEXT("The Blender export writes the mesh %s."), *ShortName(Path));
            FP4PyJson Slots = FP4PyJson::List();
            for (const FStaticMaterial& Slot : Mesh->StaticMaterials)
            {
                FP4PyJson Row = FP4PyJson::Dict();
                Row.Set(TEXT("name"), FP4PyJson::Text(Slot.MaterialSlotName.ToString()));
                Row.Set(TEXT("material"), TextOrNull(Slot.MaterialInterface));
                if (Slot.MaterialInterface)
                    Materials.Add(PackagePart(Slot.MaterialInterface->GetPathName()), Slot.MaterialInterface);
                Slots.Add(MoveTemp(Row));
            }
            const FBoxSphereBounds Bounds = Mesh->GetBounds();
            FP4PyJson Row = FP4PyJson::Dict();
            Row.Set(TEXT("file"), FP4PyJson::Text(File));
            Row.Set(TEXT("slots"), MoveTemp(Slots));
            Row.Set(TEXT("origin"), Vector3(Bounds.Origin));
            Row.Set(TEXT("extent"), Vector3(Bounds.BoxExtent));
            if (!ExportMesh(Mesh, FPaths::Combine(Folder, File)))
            {
                Row.Set(TEXT("file"), FP4PyJson::Null());
                Notes.Add(FString::Printf(TEXT("The mesh %s was not exported."), *ShortName(Path)));
            }
            SceneMeshes.Set(Path, MoveTemp(Row));
        }
        const FTransform& Transform = Part.Transform;
        FP4PyJson OnSlots = FP4PyJson::List();
        for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
        {
            UMaterialInterface* Material = Component->GetMaterial(Index);
            OnSlots.Add(TextOrNull(Material));
            if (Material)
                Materials.Add(PackagePart(Material->GetPathName()), Material);
        }
        const FQuat Rotation = Transform.GetRotation();
        FP4PyJson Row = FP4PyJson::Dict();
        Row.Set(TEXT("label"), FP4PyJson::Text(Part.Label));
        Row.Set(TEXT("mesh"), FP4PyJson::Text(Path));
        Row.Set(TEXT("materials"), MoveTemp(OnSlots));
        Row.Set(TEXT("location"), Vector3(Transform.GetTranslation()));
        FP4PyJson Quaternion = FP4PyJson::List();
        for (float Value : {Rotation.X, Rotation.Y, Rotation.Z, Rotation.W})
            Quaternion.Add(FP4PyJson::Number(Value));
        Row.Set(TEXT("rotation"), MoveTemp(Quaternion));
        Row.Set(TEXT("scale"), Vector3(Transform.GetScale3D()));
        Row.Set(TEXT("origin"), Vector3(Part.Origin));
        Row.Set(TEXT("extent"), Vector3(Part.Extent));
        if (!Component->GetOwner() || !Component->GetOwner()->IsA<AStaticMeshActor>())
        {
            Row.Set(TEXT("actor"),
                FP4PyJson::Text(Component->GetOwner() ? Component->GetOwner()->GetActorLabel() : FString()));
            Row.Set(TEXT("component"), FP4PyJson::Text(Component->GetName()));
            if (Part.Instance != INDEX_NONE)
                Row.Set(TEXT("instance"), FP4PyJson::Integer(Part.Instance));
        }
        SceneActors.Add(MoveTemp(Row));
    }
    TSet<FString> TextureSet;
    for (const FString& Path : SortedKeys(Materials))
    {
        FP4PyJson Data;
        FString Error;
        if (!MaterialData(Materials[Path], &Notes, Data, Error))
        {
            Notes.Add(
                FString::Printf(TEXT("The material %s was not exported (%s)."), *ShortName(Path), *Error.Left(160)));
            continue;
        }
        const FString File = TEXT("materials/") + FileName(Path, UsedMaterials, TEXT(".json"));
        if (!WriteText(Data.Dump(), FPaths::Combine(Folder, File)))
        {
            Out.Error = FString::Printf(TEXT("The file %s could not be written."), *FPaths::Combine(Folder, File));
            return false;
        }
        SceneMaterials.Set(Path, FP4PyJson::Text(File));
        TexturesOf(Data, TextureSet);
    }
    TArray<FString> Textures = TextureSet.Array();
    Textures.Sort(&Before);
    for (const FString& Full : Textures)
    {
        const FString Path = PackagePart(Full);
        UTexture2D* Texture = Cast<UTexture2D>(LoadAsset(Path));
        if (!Texture)
        {
            Notes.Add(
                FString::Printf(TEXT("The texture %s is not a 2D texture and was not exported."), *ShortName(Path)));
            continue;
        }
        const FString File = TEXT("textures/") + FileName(Path, UsedTextures, TEXT(".png"));
        FP4PyJson Facts = ExportTexture(Texture, FPaths::Combine(Folder, File));
        if (Facts.IsNull())
        {
            Notes.Add(
                FString::Printf(TEXT("The texture %s has no source image and was not exported."), *ShortName(Path)));
            continue;
        }
        Facts.Set(TEXT("file"), FP4PyJson::Text(File));
        SceneTextures.Set(Path, MoveTemp(Facts));
    }
    Out.Notes = Notes.Array();
    Out.Notes.Sort(&Before);
    FP4PyJson NoteList = FP4PyJson::List();
    for (const FString& Note : Out.Notes)
        NoteList.Add(FP4PyJson::Text(Note));
    Out.Actors = (int32)SceneActors.Items.size();
    Out.Meshes = (int32)SceneMeshes.Fields.size();
    Out.Materials = (int32)SceneMaterials.Fields.size();
    Out.Textures = (int32)SceneTextures.Fields.size();
    Scene.Set(TEXT("actors"), MoveTemp(SceneActors));
    Scene.Set(TEXT("meshes"), MoveTemp(SceneMeshes));
    Scene.Set(TEXT("materials"), MoveTemp(SceneMaterials));
    Scene.Set(TEXT("textures"), MoveTemp(SceneTextures));
    Scene.Set(TEXT("notes"), MoveTemp(NoteList));
    Out.Json = Scene.Dump();
    UE_LOG(LogKadanBlender, Log,
        TEXT("The Blender export wrote %d actors, %d meshes, %d materials and %d textures for %s."), Out.Actors,
        Out.Meshes, Out.Materials, Out.Textures, *SceneFile);
    return true;
}
} //namespace

bool P4ExportForBlender(UWorld* World, const FString& SceneFile, const TArray<AActor*>& Actors, FP4BlenderScene& Out)
{
    return ExportScene(World, SceneFile, Actors, {}, Out);
}

bool P4ExportMaterialsForBlender(
    const FString& SceneFile, const TArray<UMaterialInterface*>& Materials, FP4BlenderScene& Out)
{
    return ExportScene(nullptr, SceneFile, {}, Materials, Out);
}

UP4BlenderSceneExporter::UP4BlenderSceneExporter()
{
    SupportedClass = UWorld::StaticClass();
    bText = true;
    PreferredFormatIndex = 0;
    FormatExtension.Add(TEXT("json"));
    FormatDescription.Add(TEXT("Blender scene"));
}

bool UP4BlenderSceneExporter::ExportText(
    const FExportObjectInnerContext*, UObject* Object, const TCHAR*, FOutputDevice& Ar, FFeedbackContext*, uint32)
{
    UWorld* World = Cast<UWorld>(Object);
    const FString SceneFile = UExporter::CurrentFilename;
    if (!World || SceneFile.IsEmpty())
        return false;
    FP4BlenderScene Scene;
    if (!P4ExportForBlender(
            World, FPaths::ConvertRelativePathToFull(SceneFile), P4BlenderSceneActors(World, bSelectedOnly), Scene))
    {
        UE_LOG(LogKadanBlender, Error, TEXT("%s"), *Scene.Error);
        Notify(Scene.Error, false);
        return false;
    }
    Ar.Log(*Scene.Json);
    FString Message = FString::Printf(
        TEXT("Wrote %d objects with %d meshes, %d materials and %d textures for Blender.\n")
            TEXT("In Blender, open the TransMat panel and choose Import from Unreal with %s."),
        Scene.Actors, Scene.Meshes, Scene.Materials, Scene.Textures, *FPaths::GetCleanFilename(SceneFile));
    if (Scene.Notes.Num())
        Message += FString::Printf(TEXT("\nThe scene file lists %d notes."), Scene.Notes.Num());
    Notify(Message, true);
    return true;
}

UP4BlenderMaterialExporter::UP4BlenderMaterialExporter()
{
    SupportedClass = UMaterialInterface::StaticClass();
    bText = true;
    PreferredFormatIndex = 0;
    FormatExtension.Add(TEXT("json"));
    FormatDescription.Add(TEXT("Blender materials"));
}

bool UP4BlenderMaterialExporter::ExportText(
    const FExportObjectInnerContext*, UObject* Object, const TCHAR*, FOutputDevice& Ar, FFeedbackContext*, uint32)
{
    UMaterialInterface* Material = Cast<UMaterialInterface>(Object);
    const FString SceneFile = UExporter::CurrentFilename;
    if (!Material || SceneFile.IsEmpty())
        return false;
    FP4BlenderScene Scene;
    if (!P4ExportMaterialsForBlender(FPaths::ConvertRelativePathToFull(SceneFile), {Material}, Scene))
    {
        UE_LOG(LogKadanBlender, Error, TEXT("%s"), *Scene.Error);
        return false;
    }
    Ar.Log(*Scene.Json);
    return true;
}

FString UKadanBlender::ExportForBlender(const FString& SceneFile, const TArray<AActor*>& Actors)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext(false).World() : nullptr;
    TArray<AActor*> Chosen = Actors;
    if (!Actors.Num())
        Chosen = P4BlenderSceneActors(World, true);
    if (Chosen.Num() && Chosen[0]->GetWorld())
        World = Chosen[0]->GetWorld();
    FP4BlenderScene Scene;
    const FString File = FPaths::ConvertRelativePathToFull(SceneFile);
    if (!P4ExportForBlender(World, File, Chosen, Scene))
    {
        UE_LOG(LogKadanBlender, Error, TEXT("%s"), *Scene.Error);
        return FString();
    }
    if (!WriteText(Scene.Json, File))
    {
        UE_LOG(LogKadanBlender, Error, TEXT("The file %s could not be written."), *File);
        return FString();
    }
    return Scene.Json;
}

FString UKadanBlender::ExportMaterialsForBlender(const FString& SceneFile, const TArray<UMaterialInterface*>& Materials)
{
    FP4BlenderScene Scene;
    const FString File = FPaths::ConvertRelativePathToFull(SceneFile);
    if (!Materials.Num())
    {
        UE_LOG(LogKadanBlender, Warning, TEXT("No materials were given, so nothing was written."));
        return FString();
    }
    if (!P4ExportMaterialsForBlender(File, Materials, Scene))
    {
        UE_LOG(LogKadanBlender, Error, TEXT("%s"), *Scene.Error);
        return FString();
    }
    if (!WriteText(Scene.Json, File))
    {
        UE_LOG(LogKadanBlender, Error, TEXT("The file %s could not be written."), *File);
        return FString();
    }
    return Scene.Json;
}

namespace
{
// The game's packages as the Kadan panel's Import page reads them: the original containers through
// retoc (its normal Game folder), or with Use extracted packages an extracted folder.
struct FGameSource
{
    FP4KadanImportSettings Settings;
    bool Extracted = false;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Assets;
    TMap<FString, FString> Kinds;
    FString Error;
};

bool ReadGameSource(const FString& Location, const FString& Retoc, bool Extracted, FGameSource& Out)
{
    Out = FGameSource();
    Out.Extracted = Extracted;
    if (Extracted)
    {
        Out.Settings.GameFolder = Location;
        TArray<FString> Problems;
        P4KadanScanGame(Location, Out.Assets, Problems);
        for (const FString& Problem : Problems)
            UE_LOG(LogKadanBlender, Warning, TEXT("Kadan scan: %s"), *Problem);
        for (const auto& Asset : Out.Assets)
            if (Asset)
                Out.Kinds.Add(Asset->Package, Asset->Kind);
        return Out.Kinds.Num() > 0;
    }
    FP4KadanSourceCatalog Catalog;
    if (!P4KadanReadSourceCatalog(Location, Retoc, Catalog, Out.Error))
        return false;
    Out.Settings = Catalog.ImportSettings;
    for (const auto& Row : Catalog.Rows)
        if (Row)
            Out.Kinds.Add(Row->Package, Row->Kind);
    return true;
}

FP4KadanImportResult ImportFromGame(
    FGameSource& Source, const TSet<FString>& Roots, FP4KadanImportPlan& Plan, bool Resume = true)
{
    // The Import page's route for selected packages: plan them with what they need, then import.
    Plan = Source.Extracted ? P4KadanPlanImport(Source.Assets, Roots, Resume)
                            : P4KadanPlanImport(Source.Settings, Roots, Resume);
    for (const FString& Problem : Plan.Problems)
        UE_LOG(LogKadanBlender, Warning, TEXT("Kadan import review: %s"), *Problem);
    return P4KadanRunImport(Source.Settings, Plan, Resume);
}

FP4PyJson TextList(const TArray<FString>& Lines)
{
    FP4PyJson Out = FP4PyJson::List();
    for (const FString& Line : Lines)
        Out.Add(FP4PyJson::Text(Line));
    return Out;
}
} //namespace

FString UKadanBlender::ImportGameMaterial(const FString& Path)
{
    const FString Package = PackagePart(Path);
    const FString Name = ShortName(Package);
    if (Cast<UMaterialInterface>(LoadAsset(Package)))
        return FString();
    // The game folder and tools the Kadan panel's Import page keeps for this project.
    FString Location, GameFolder, Retoc, GamePaks, Repak;
    bool Extracted = false;
    P4KadanSettings::Read(P4KadanSettings::GameLocation, Location);
    P4KadanSettings::Read(P4KadanSettings::GameFolder, GameFolder);
    P4KadanSettings::Read(P4KadanSettings::GamePaks, GamePaks);
    P4KadanSettings::Read(P4KadanSettings::Retoc, Retoc);
    P4KadanSettings::Read(P4KadanSettings::Repak, Repak);
    P4KadanSettings::Read(P4KadanSettings::UseExtractedPackages, Extracted);
    if (Extracted)
        Location = GameFolder;
    if (Location.IsEmpty())
        return FString::Printf(
            TEXT("The game material %s is not in this project, and no game folder is set in Kadan. ")
                TEXT("Set the game folder on the Import page of the Kadan panel and run the script again."),
            *Name);
    FString Why;
    if (!P4KadanProjectReady(Why))
        return Why;
    FGameSource Source;
    if (!ReadGameSource(Location, Retoc, Extracted, Source))
    {
        UE_LOG(LogKadanBlender, Warning, TEXT("Kadan game folder: %s"), *Source.Error);
        return FString::Printf(
            TEXT("Kadan could not read the game folder set on its Import page, so the game material %s ")
                TEXT("was not imported. The Output Log says why."),
            *Name);
    }
    if (Extracted)
    {
        Source.Settings.GamePaks = GamePaks;
        Source.Settings.Retoc = Retoc;
    }
    Source.Settings.Repak = Repak;
    bool KnownPackage = false;
    for (const auto& Row : Source.Kinds)
        if (Row.Key.Equals(Package, ESearchCase::IgnoreCase))
        {
            KnownPackage = true;
            break;
        }
    if (!KnownPackage)
        return FString::Printf(TEXT("The game material %s is not in this project or in the game folder set in Kadan. ")
                                   TEXT("Check the game folder on the Import page of the Kadan panel."),
            *Name);
    FP4KadanImportPlan Plan;
    const FP4KadanImportResult Result = ImportFromGame(Source, {Package}, Plan);
    for (const FString& Problem : Result.Problems)
        UE_LOG(LogKadanBlender, Warning, TEXT("Kadan import: %s"), *Problem);
    if (!Cast<UMaterialInterface>(LoadAsset(Package)))
        return FString::Printf(TEXT("Kadan could not import the game material %s. The Output Log says why."), *Name);
    UE_LOG(LogKadanBlender, Log, TEXT("Kadan imported the game material %s for a material instance."), *Package);
    return FString();
}

FString UKadanBlender::GameCatalogJson(const FString& Location, const FString& Retoc, bool Extracted)
{
    FGameSource Source;
    FP4PyJson Out = FP4PyJson::Dict();
    const bool Ok = ReadGameSource(Location, Retoc, Extracted, Source);
    Out.Set(TEXT("ok"), FP4PyJson::Boolean(Ok));
    Out.Set(TEXT("error"), FP4PyJson::Text(Source.Error));
    FP4PyJson Packages = FP4PyJson::Dict();
    TArray<FString> Names;
    Source.Kinds.GetKeys(Names);
    Names.Sort();
    for (const FString& Name : Names)
        Packages.Set(Name, FP4PyJson::Text(Source.Kinds[Name]));
    Out.Set(TEXT("packages"), MoveTemp(Packages));
    return Out.Dump();
}

FString UKadanBlender::ImportGamePackages(const FString& Location, const FString& Retoc,
    const TArray<FString>& Packages, bool Extracted, const FString& Repak, bool Reimport)
{
    FP4PyJson Out = FP4PyJson::Dict();
    FString Why;
    FGameSource Source;
    if (!P4KadanProjectReady(Why))
        Out.Set(TEXT("error"), FP4PyJson::Text(Why));
    else if (!ReadGameSource(Location, Retoc, Extracted, Source))
        Out.Set(TEXT("error"), FP4PyJson::Text(Source.Error));
    else
    {
        if (Extracted)
            Source.Settings.Retoc = Retoc;
        Source.Settings.Repak = Repak;
        FP4KadanImportPlan Plan;
        const FP4KadanImportResult Result = ImportFromGame(Source, TSet<FString>(Packages), Plan, !Reimport);
        Out.Set(TEXT("ok"), FP4PyJson::Boolean(Result.Ok));
        Out.Set(TEXT("roots"), FP4PyJson::Integer(Plan.Roots));
        Out.Set(TEXT("dependencies"), FP4PyJson::Integer(Plan.Dependencies));
        Out.Set(TEXT("already_present"), FP4PyJson::Integer(Plan.AlreadyPresent));
        Out.Set(TEXT("plan_problems"), TextList(Plan.Problems));
        FP4PyJson Imported = FP4PyJson::Dict();
        TArray<FString> Names;
        Result.Imported.GetKeys(Names);
        Names.Sort();
        for (const FString& Name : Names)
            Imported.Set(Name, FP4PyJson::Text(Result.Imported[Name]));
        Out.Set(TEXT("imported"), MoveTemp(Imported));
        Out.Set(TEXT("not_imported"), TextList(Result.NotImported));
        Out.Set(TEXT("reference_only"), TextList(Result.ReferenceOnly));
        Out.Set(TEXT("problems"), TextList(Result.Problems));
        Out.Set(TEXT("warnings"), TextList(Result.Warnings));
    }
    return Out.Dump();
}

FString UKadanBlender::BlenderMaterialJson(UMaterialInterface* Material)
{
    FP4PyJson Data;
    FString Error;
    if (!Material || !MaterialData(Material, nullptr, Data, Error))
        return FString();
    return Data.Dump();
}
