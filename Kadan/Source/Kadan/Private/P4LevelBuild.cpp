// Making a level's world, actors and objects from what the game's file says.

#include "P4LevelContentInternal.h"
#include "P4Common.h"

namespace P4LevelContent
{
// Gives an object the id the game's file says it carries, so that the lazy references that name it find it.
// An object of an earlier import thrown away may still hold the id; it gives it up.
bool GiveObjectGuid(UObject* Object, const FString& Digits)
{
    if (Digits.Len() != 32)
        return false;
    uint32 Words[4];
    for (int32 Word = 0; Word < 4; ++Word)
        Words[Word] = uint32(FCString::Strtoui64(*Digits.Mid(Word * 8, 8), nullptr, 16));
    const FGuid Id(Words[0], Words[1], Words[2], Words[3]);
    UObject* Holder = FUniqueObjectGuid(Id).ResolveObject();
    if (Holder == Object)
        return true;
    // The engine moves an id from an object that has a newer version, as it does when a package is loaded again.
    if (Holder && (Holder->IsPendingKill() || Holder->GetOutermost() == GetTransientPackage()))
        Holder->SetFlags(RF_NewerVersionExists);
    TArray<uint8> Bytes;
    Bytes.SetNumZeroed(20);
    const uint32 One = 1;
    FMemory::Memcpy(Bytes.GetData(), &One, 4);
    FMemory::Memcpy(Bytes.GetData() + 4, Words, 16);
    FMemoryReader Reader(Bytes);
    FStructuredArchiveFromArchive Adapter(Reader);
    FLazyObjectPtr::PossiblySerializeObjectGuid(Object, Adapter.GetSlot().EnterRecord());
    return FUniqueObjectGuid(Id).ResolveObject() == Object;
}

// Gives a landscape component's collision component the heights of the component's height map, as the
// landscape editor does (ULandscapeComponent::UpdateCollisionHeightData, which the engine does not export), and
// has the engine make the collision again. A cook makes the collision it ships from these heights. Only the kind
// of collision the game's landscapes have is made: from the full heights, without simpler collision, offsets or
// heights a material moves.
bool GiveLandscapeCollisionHeights(ULandscapeComponent* Component, FString& Error)
{
    const auto Number = [](const UObject* Object, const TCHAR* Name, int64& Out)
    {
        const FNumericProperty* Property = Object ? FindFProperty<FNumericProperty>(Object->GetClass(), Name) : nullptr;
        if (!Property || !Property->IsInteger())
            return false;
        Out = Property->GetSignedIntPropertyValue(Property->ContainerPtrToValuePtr<void>(Object));
        return true;
    };
    const auto Flag = [](const UObject* Object, const TCHAR* Name, bool& Out)
    {
        const FBoolProperty* Property = Object ? FindFProperty<FBoolProperty>(Object->GetClass(), Name) : nullptr;
        if (!Property)
            return false;
        Out = Property->GetPropertyValue_InContainer(Object);
        return true;
    };
    ULandscapeHeightfieldCollisionComponent* Collision = Component->CollisionComponent.Get();
    UTexture2D* Heightmap = Component->GetHeightmap();
    const FObjectPropertyBase* Offsets = FindFProperty<FObjectPropertyBase>(
        Component->GetClass(), TEXT("XYOffsetmapTexture"));
    const FStructProperty* Bias = FindFProperty<FStructProperty>(Component->GetClass(), TEXT("HeightmapScaleBias"));
    int64 Mip = 0, Simple = 0, Subsections = 0, SubsectionQuads = 0;
    bool Baked = false;
    if (!Collision || !Heightmap || !Offsets || !Bias || Bias->Struct != TBaseStructure<FVector4>::Get() ||
        !Number(Component, TEXT("CollisionMipLevel"), Mip) ||
        !Number(Component, TEXT("SimpleCollisionMipLevel"), Simple) ||
        !Number(Component, TEXT("NumSubsections"), Subsections) ||
        !Number(Component, TEXT("SubsectionSizeQuads"), SubsectionQuads) ||
        !Flag(Component->GetOuter(), TEXT("bBakeMaterialPositionOffsetIntoCollision"), Baked))
    {
        Error = TEXT("level-import-landscape-component-not-joined-to-its-parts");
        return false;
    }
    if (Collision->GetClass() != ULandscapeHeightfieldCollisionComponent::StaticClass() || Simple > Mip || Baked ||
        Offsets->GetObjectPropertyValue_InContainer(Component) || Mip < 0 || Mip > 8 || Subsections < 1 ||
        Subsections > 2 || SubsectionQuads < 1)
    {
        Error = TEXT("level-import-landscape-collision-of-a-kind-not-brought-over-yet");
        return false;
    }
    TArray64<uint8> Pixels;
    const FTextureSource& Source = Heightmap->Source;
    const int32 SizeU = Source.GetSizeX(), SizeV = Source.GetSizeY(), MipU = SizeU >> Mip, MipV = SizeV >> Mip;
    if (Source.GetFormat() != TSF_BGRA8 || Mip >= Source.GetNumMips() ||
        !const_cast<FTextureSource&>(Source).GetMipData(Pixels, int32(Mip)) || Pixels.Num() != int64(MipU) * MipV * 4)
    {
        Error = TEXT("level-import-landscape-height-map-unreadable");
        return false;
    }
    const FVector4 ScaleBias = *Bias->ContainerPtrToValuePtr<FVector4>(Component);
    const int32 SubVerts = int32((SubsectionQuads + 1) >> Mip), SubQuads = SubVerts - 1,
                SizeVerts = int32(Subsections) * SubQuads + 1;
    const int32 OffsetX = FMath::RoundToInt(ScaleBias.Z * float(SizeU)) >> Mip,
                OffsetY = FMath::RoundToInt(ScaleBias.W * float(SizeV)) >> Mip;
    TArray<uint16> Heights;
    Heights.SetNumZeroed(SizeVerts * SizeVerts);
    const FColor* Texels = reinterpret_cast<const FColor*>(Pixels.GetData());
    for (int32 SubY = 0; SubY < Subsections; ++SubY)
        for (int32 SubX = 0; SubX < Subsections; ++SubX)
            for (int32 Y = 0; Y <= SubQuads; ++Y)
                for (int32 X = 0; X <= SubQuads; ++X)
                {
                    const int32 TexX = OffsetX + SubVerts * SubX + X, TexY = OffsetY + SubVerts * SubY + Y;
                    if (TexX < 0 || TexY < 0 || TexX >= MipU || TexY >= MipV)
                    {
                        Error = TEXT("level-import-landscape-height-map-too-small");
                        return false;
                    }
                    const FColor& Texel = Texels[TexX + TexY * MipU];
                    Heights[SubQuads * SubX + X + (SubQuads * SubY + Y) * SizeVerts] = uint16(Texel.R << 8 | Texel.G);
                }
    Collision->CollisionHeightData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(
        Collision->CollisionHeightData.Realloc(Heights.Num()), Heights.GetData(), Heights.Num() * sizeof(uint16));
    Collision->CollisionHeightData.Unlock();
    Collision->RecreateCollision();
    return true;
}

// Takes a level that is not kept out of its place.
void Discard(UWorld* World)
{
    UPackage* Package = World->GetOutermost();
    World->ClearFlags(RF_Public | RF_Standalone);
    World->DestroyWorld(false);
    World->MarkPendingKill();
    Package->SetDirtyFlag(false);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
}

// A constructor-owned component can use the exact qualified native storage child
// of its original engine type. The source default-subobject flag and explicit
// template must agree with the live constructor object; subclassing alone does
// not establish this identity.
bool ExistingStorageSubobject(
    UObject* Object, UObject* Outer, UClass* Declared, const FString& Name, uint32 SourceFlags, UObject* SourceTemplate)
{
    return IsValid(Object) && IsValid(Outer) && Cast<AActor>(Outer) && Cast<UActorComponent>(Object) &&
        Object->GetOuter() == Outer && P4SameText(Object->GetName(), Name) &&
        (SourceFlags & uint32(RF_DefaultSubObject)) && Object->HasAnyFlags(RF_DefaultSubObject) &&
        Object->GetClass() != Declared && P4BlueprintSourceSubobjectClassMatches(Object->GetClass(), Declared) &&
        IsValid(SourceTemplate) && SourceTemplate->GetClass() == Object->GetClass() &&
        SourceTemplate->GetOuter() == Outer->GetClass()->GetDefaultObject(false) &&
        P4SameText(SourceTemplate->GetName(), Name) && SourceTemplate->HasAnyFlags(RF_DefaultSubObject) &&
        Object->GetArchetype() == SourceTemplate;
}

// A placed component may explicitly clear the class its SCS template
// spawns. Applying the reflected field does not call UE's destruction
// path, so retire that constructor child before exact source cooking.
// True when the component's child was retired; the component is then named.
bool RetireClearedInheritedChild(UChildActorComponent* Component, const FDecodedValue* ChildClass,
    const FDecodedValue* Child, TSet<UChildActorComponent*>& Named)
{
    const auto IsNull = [](const FDecodedValue* Value)
    {
        return Value &&
            (Value->Kind == FDecodedValue::EKind::Null ||
                (Value->Kind == FDecodedValue::EKind::HardReference && Value->ReferenceIndex == 0));
    };
    if (Component && !Named.Contains(Component) && IsNull(ChildClass) && (!Child || IsNull(Child)) &&
        !Component->GetChildActorClass())
    {
        Component->DestroyChildActor();
        Named.Add(Component);
        return true;
    }
    return false;
}

// Makes the level new, as the editor makes one, and gives it what the game's file says. NotHeld takes
// the values this engine has no place for: properties the game's engine adds to the engine's own
// classes and structs. NotShipped takes where a reference is to something the game does not ship.
// NotInGame takes the components a class of the project makes that the game's file does not have.
UWorld* Build(const FFile& File, TArray<FString>& NotHeld, TArray<FString>& NotShipped, TArray<FString>& NotInGame,
    FString& Error)
{
    const FNode& SettingsNode = File.Node(File.Settings);
    UClass* SettingsClass = LoadObject<UClass>(nullptr, *SettingsNode.Class, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!SettingsClass)
    {
        Error = TEXT("level-import-class-not-in-the-project: ") + SettingsNode.Class;
        return nullptr;
    }
    // The engine replaces a level's world settings by one of the project's class when the level loads.
    if (!GEngine || !GEditor || GEngine->WorldSettingsClass != SettingsClass)
    {
        Error = TEXT("level-import-world-settings-class-is-not-the-project's-own: ") + SettingsNode.Class;
        return nullptr;
    }
    const FString Short = FPackageName::GetLongPackageAssetName(File.PackageName);
    UWorld* World = P4CreateOriginalImportWorld(FName(*Short),
        CreatePackage(*(File.ConstructionPackage.IsEmpty() ? File.PackageName : File.ConstructionPackage)));
    if (!World)
    {
        Error = TEXT("level-import-engine-made-no-level");
        return nullptr;
    }
    // Every level the editor makes has a builder brush as its second actor. A cook leaves it out, and
    // the game's file has the gap.
    GEditor->InitBuilderBrush(World);
    World->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
    const auto Fail = [&](const FString& Why)
    {
        Error = Why;
        Discard(World);
        return static_cast<UWorld*>(nullptr);
    };
    ULevel* Level = World->PersistentLevel;
    AWorldSettings* Settings = Level ? Level->GetWorldSettings(false) : nullptr;
    if (!Level || !Level->Model || !Settings || Settings->GetClass() != SettingsClass)
        return Fail(TEXT("level-import-engine-made-no-level"));
    // The level's model is numbered by how many models the editor had made before it.
    if (!P4SameText(Level->Model->GetName(), File.Node(File.Model).Name))
        Level->Model->Rename(*File.Node(File.Model).Name, nullptr,
            REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
    if (!P4SameText(Level->GetName(), File.Node(File.Level).Name) ||
        !P4SameText(Level->Model->GetName(), File.Node(File.Model).Name) ||
        !P4SameText(Settings->GetName(), SettingsNode.Name))
        return Fail(TEXT("level-import-engine-names-the-level's-objects-differently"));
    TMap<int32, UObject*> Made;
    Made.Add(File.World, World);
    Made.Add(File.Level, Level);
    Made.Add(File.Model, Level->Model);
    Made.Add(File.Settings, Settings);
    AActor* Brush = Level->Actors.Num() > 1 ? Level->Actors[1] : nullptr;
    // The levels this one loads, in the file's order. Each names its level and is not loaded here.
    for (const std::int32_t Index : File.WorldNative.StreamingLevels)
    {
        const FNode& Node = File.Node(Index);
        UClass* Class = LoadObject<UClass>(nullptr, *Node.Class, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Class || !Class->IsChildOf(ULevelStreaming::StaticClass()))
            return Fail(TEXT("level-import-class-not-in-the-project: ") + Node.Class);
        ULevelStreaming* Streaming = NewObject<ULevelStreaming>(World, Class, FName(*Node.Name), RF_Transactional);
        World->AddStreamingLevel(Streaming);
        Made.Add(Index, Streaming);
    }
    FString Why;
    TArray<UActorComponent*> Added;
    TSet<int32> Given;
    // The child actor components whose child has the name the game's file has for it.
    TSet<UChildActorComponent*> Named;
    // The level's actors. An actor that a component of another actor makes (a child actor) says which
    // component, and is there once that component has its settings: it is found, not made.
    const auto Actors = [&](bool& Progress)
    {
        for (const int32 Index : File.Actors)
        {
            if (Made.Contains(Index))
                continue;
            const FNode& Node = File.Node(Index);
            UClass* Class = LoadObject<UClass>(nullptr, *Node.Class, nullptr, LOAD_NoWarn | LOAD_Quiet);
            if (!Class || !Class->IsChildOf(AActor::StaticClass()))
            {
                Why = TEXT("level-import-class-not-in-the-project: ") + Node.Class;
                return false;
            }
            UObject* There = StaticFindObjectFast(nullptr, Level, FName(*Node.Name));
            AActor* Actor = Cast<AActor>(There);
            // A child actor is the file's only once its component has given it the file's name. Before
            // that an actor of that name is some component's child under the engine's own numbering.
            if (Node.Values.Field("ParentComponent") != nullptr &&
                (!Actor || !Actor->GetParentComponent() || !Named.Contains(Actor->GetParentComponent())))
                continue;
            if (There && (!Actor || Actor->GetClass() != Class || Actor->IsPendingKill()))
            {
                Why = TEXT("level-import-engine-made-another-object-there: ") + Node.Path;
                return false;
            }
            if (!Actor)
            {
                FActorSpawnParameters Spawn;
                Spawn.Name = FName(*Node.Name);
                Spawn.OverrideLevel = Level;
                Spawn.ObjectFlags = RF_Transactional;
                Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                Actor = World->SpawnActor(Class, nullptr, nullptr, Spawn);
            }
            if (!Actor)
            {
                Why = TEXT("level-import-actor-not-made: ") + Node.Path;
                return false;
            }
            // A level knows its foliage actor, as it does once it is loaded.
            if (AInstancedFoliageActor* Foliage = Cast<AInstancedFoliageActor>(Actor))
                if (!Level->InstancedFoliageActor.IsValid())
                    Level->InstancedFoliageActor = Foliage;
            Made.Add(Index, Actor);
            Progress = true;
        }
        return true;
    };
    // The objects inside the world settings and the actors, each after the one it is in. One the
    // class made is found. One the class did not make is made, and a component among those is the
    // actor's own.
    const auto Inside = [&](bool& Progress)
    {
        for (bool More = true; More;)
        {
            More = false;
            for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
            {
                const FNode& Node = File.Node(Index);
                // An actor itself is spawned, or made by the component it is the child of. Never here.
                // An instanced component of foliage is made by the foliage actor's own code.
                if (Made.Contains(Index) || File.Actors.Contains(Index) || File.Brushes.Contains(Index) ||
                    File.Instanced.Contains(Index) || !(File.Below(Index, File.Settings) || File.InActor(Index)) ||
                    !Made.Contains(Node.Outer))
                    continue;
                UClass* Class = LoadObject<UClass>(nullptr, *Node.Class, nullptr, LOAD_NoWarn | LOAD_Quiet);
                if (!Class)
                {
                    Why = TEXT("level-import-class-not-in-the-project: ") + Node.Class;
                    return false;
                }
                UObject* Outer = Made[Node.Outer];
                UObject* Object = StaticFindObjectFast(nullptr, Outer, FName(*Node.Name));
                if (Object && Object->GetClass() != Class)
                {
                    if (!P4BlueprintSourceSubobjectClassMatches(Object->GetClass(), Class))
                    {
                        Why = TEXT("level-import-engine-made-another-object-there: ") + Node.Path;
                        return false;
                    }
                    UObject* SourceTemplate = Node.Template ? DeclaredTemplate(File, Index, Made, nullptr, Why)
                                                            : nullptr;
                    if (!ExistingStorageSubobject(Object, Outer, Class, Node.Name, Node.Flags, SourceTemplate))
                    {
                        Why = TEXT("level-import-engine-made-another-object-there: ") + Node.Path;
                        return false;
                    }
                }
                if (!Object)
                {
                    UObject* Template = DeclaredTemplate(
                        File, Index, Made, Node.Template ? nullptr : Class->GetDefaultObject(), Why);
                    if (!Template)
                    {
                        if (File.Export(Node.Template) && !Made.Contains(Node.Template))
                            continue;
                        return false;
                    }
                    if (!Class->IsChildOf(Template->GetClass()))
                    {
                        Why = TEXT("level-import-template-class-differs: ") + Node.Path;
                        return false;
                    }
                    Object = NewObject<UObject>(Outer, Class, FName(*Node.Name),
                        RF_Transactional |
                            EObjectFlags(Node.Flags &
                                uint32(RF_Public | RF_ArchetypeObject | RF_DefaultSubObject |
                                    RF_InheritableComponentTemplate)),
                        Template);
                    // Whether it is one of the components added to the actor in the level is what the
                    // actor's own list in the file says.
                    UActorComponent* Component = Cast<UActorComponent>(Object);
                    if (Cast<AActor>(Outer) && Component)
                        Added.Add(Component);
                }
                if (Node.Lods >= 0 && !Object->IsA<UStaticMeshComponent>())
                {
                    Why = TEXT("level-import-native-data-not-brought-over-yet: ") + Node.Path + TEXT(" (") +
                        Node.Class + TEXT(")");
                    return false;
                }
                Made.Add(Index, Object);
                More = true;
                Progress = true;
            }
        }
        return true;
    };
    // The shape of a volume. Its model is made as the editor makes a brush's: from its polygons, which
    // are read back out of the game's model (each polygon of a convex brush is one node of its tree),
    // by the engine's own build of the tree and of the collision.
    TSet<int32> Shaped;
    const auto Volumes = [&](bool& Progress)
    {
        for (const auto& Entry : File.Brushes)
        {
            const int32 ModelIndex = Entry.Key;
            const P4MapKit::FModelNative& Shape = Entry.Value;
            const FNode& ModelNode = File.Node(ModelIndex);
            UObject** Owner = Made.Find(ModelNode.Outer);
            ABrush* Volume = Owner ? Cast<ABrush>(*Owner) : nullptr;
            if (Shaped.Contains(ModelIndex) || !Owner)
                continue;
            const FString Where = ModelNode.Path;
            if (!Volume || !Volume->GetBrushComponent())
            {
                Why = TEXT("level-import-model-of-something-that-is-no-brush: ") + Where;
                return false;
            }
            const auto Vector = [](const std::vector<std::array<float, 3>>& List, std::int32_t At, FVector& Out)
            {
                if (At < 0 || std::size_t(At) >= List.size())
                    return false;
                Out = FVector(List[At][0], List[At][1], List[At][2]);
                return true;
            };
            // The polygons, in the order the brush had them.
            TArray<int32> Order;
            for (int32 Row = 0; Row < int32(Shape.Nodes.size()); ++Row)
                Order.Add(Row);
            bool Sound = Shape.Nodes.size() == Shape.Surfaces.size() && Shape.BufferVertices == 0;
            for (int32 Row = 0; Sound && Row < Order.Num(); ++Row)
                Sound = Shape.Nodes[Row].Surface >= 0 && std::size_t(Shape.Nodes[Row].Surface) < Shape.Surfaces.size();
            if (!Sound)
            {
                Why = TEXT("level-import-brush-shape-not-brought-over-yet: ") + Where;
                return false;
            }
            Order.StableSort(
                [&](int32 A, int32 B)
                {
                    return Shape.Surfaces[Shape.Nodes[A].Surface].BrushPolygon <
                        Shape.Surfaces[Shape.Nodes[B].Surface].BrushPolygon;
                });
            UModel* Model = NewObject<UModel>(Volume, FName(*ModelNode.Name), RF_Transactional);
            Model->Initialize(nullptr, true);
            Model->Polys = NewObject<UPolys>(Model, NAME_None, RF_Transactional);
            for (const int32 Row : Order)
            {
                const P4MapKit::FModelNode& Polygon = Shape.Nodes[Row];
                const P4MapKit::FModelSurface& Surface = Shape.Surfaces[Polygon.Surface];
                FPoly Face;
                Face.Init();
                // The polygon's own normal is the one its plane has. The one the surface names in the
                // model's list may be another polygon's vector that was near enough to stand for it.
                FVector Listed;
                Face.Normal = FVector(Surface.Plane[0], Surface.Plane[1], Surface.Plane[2]);
                Sound = Surface.Material == 0 && Vector(Shape.Points, Surface.Base, Face.Base) &&
                    Vector(Shape.Vectors, Surface.Normal, Listed) &&
                    Vector(Shape.Vectors, Surface.TextureU, Face.TextureU) &&
                    Vector(Shape.Vectors, Surface.TextureV, Face.TextureV) && Polygon.VertexPool >= 0 &&
                    std::size_t(Polygon.VertexPool) + Polygon.Vertices <= Shape.Vertices.size() &&
                    Polygon.Vertices >= 3;
                for (int32 Corner = 0; Sound && Corner < int32(Polygon.Vertices); ++Corner)
                {
                    FVector Point;
                    Sound = Vector(Shape.Points, Shape.Vertices[Polygon.VertexPool + Corner].Point, Point);
                    Face.Vertices.Add(Point);
                }
                if (!Sound)
                {
                    Model->MarkPendingKill();
                    Why = TEXT("level-import-brush-shape-not-brought-over-yet: ") + Where;
                    return false;
                }
                Face.PolyFlags = Surface.PolyFlags;
                Face.LightMapScale = Surface.LightMapScale;
                Model->Polys->Element.Add(Face);
            }
            // A brush builder marks the brush it has made as linked.
            Model->Linked = Shape.Linked;
            Volume->Brush = Model;
            Volume->GetBrushComponent()->Brush = Model;
            FBSPOps::csgPrepMovingBrush(Volume);
            Made.Add(ModelIndex, Model);
            Shaped.Add(ModelIndex);
            Progress = true;
            // The collision the engine made for the shape takes the place the game's file has for it.
            int32 BodyIndex = 0;
            for (int32 Index = 1; Index <= File.Nodes.Num(); ++Index)
                if (File.Below(Index, ModelNode.Outer) &&
                    P4SameText(File.Node(Index).Class, TEXT("/Script/Engine.BodySetup")))
                {
                    BodyIndex = BodyIndex ? -1 : Index;
                }
            UBodySetup* Collision = Volume->GetBrushComponent()->BrushBodySetup;
            if (BodyIndex < 0 || (BodyIndex > 0) != (Collision != nullptr))
            {
                Why = TEXT("level-import-brush-collision-differs: ") + Where;
                return false;
            }
            if (Collision)
            {
                const FNode& BodyNode = File.Node(BodyIndex);
                if (!P4SameText(Collision->GetName(), BodyNode.Name) &&
                    !Collision->Rename(*BodyNode.Name, nullptr,
                        REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders))
                {
                    Why = TEXT("level-import-brush-collision-not-named: ") + Where;
                    return false;
                }
                Made.Add(BodyIndex, Collision);
            }
        }
        return true;
    };
    // Foliage. Each kind is added to the foliage actor as the editor's foliage mode adds it, which makes
    // the component that draws it, and its instances are added where the game's file has them. What the
    // editor keeps of an instance besides (what it stands on, how it was aligned) is not in a cooked file.
    TSet<int32> Planted;
    const auto Foliage = [&](bool& Progress)
    {
        for (const auto& Entry : File.Foliage)
        {
            UObject** Owner = Made.Find(Entry.Key);
            if (Planted.Contains(Entry.Key) || !Owner)
                continue;
            const FString Where = File.Node(Entry.Key).Path;
            AInstancedFoliageActor* Actor = Cast<AInstancedFoliageActor>(*Owner);
            if (!Actor || !Actor->GetRootComponent())
            {
                Why = TEXT("level-import-foliage-of-something-that-is-no-foliage-actor: ") + Where;
                return false;
            }
            for (const P4MapKit::FFoliagePlaced& One : Entry.Value.Placed)
            {
                const FString TypePath = File.Ref(One.Type);
                UFoliageType* Type = Cast<UFoliageType>(StaticLoadObject(
                    UFoliageType::StaticClass(), nullptr, *TypePath, nullptr, LOAD_NoWarn | LOAD_Quiet));
                if (!Type)
                {
                    Why = TEXT("level-import-asset-not-in-the-project: ") + TypePath + TEXT(": ") + Where;
                    return false;
                }
                if (Actor->FindInfo(Type))
                {
                    Why = TEXT("level-import-foliage-kind-is-there-twice: ") + TypePath;
                    return false;
                }
                FFoliageInfo* Info = Actor->AddMesh(Type);
                if (!Info || Info->Type != EFoliageImplType::StaticMesh)
                {
                    Why = TEXT("level-import-foliage-kind-not-brought-over-yet: ") + TypePath;
                    return false;
                }
                // A kind that was listed and never placed keeps no component, as the editor leaves it.
                if (!One.Component)
                    continue;
                const FNode& ComponentNode = File.Node(One.Component);
                const P4MapKit::FInstancedMeshNative& Said = File.Instanced[One.Component];
                if (!Info->IsInitialized())
                    Info->Initialize(Actor, Type);
                UHierarchicalInstancedStaticMeshComponent* Component = Info->GetComponent();
                if (!Component || Component->GetOuter() != Actor)
                {
                    Why = TEXT("level-import-foliage-component-not-made: ") + ComponentNode.Path;
                    return false;
                }
                if (!P4SameText(Component->GetClass()->GetPathName(), ComponentNode.Class))
                {
                    Why = TEXT("level-import-foliage-component-of-another-class: ") + ComponentNode.Path + TEXT(" (") +
                        Component->GetClass()->GetPathName() + TEXT(")");
                    return false;
                }
                if (!P4SameText(Component->GetName(), ComponentNode.Name) &&
                    (StaticFindObjectFast(nullptr, Actor, FName(*ComponentNode.Name)) ||
                        !Component->Rename(*ComponentNode.Name, nullptr,
                            REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders)))
                {
                    Why = TEXT("level-import-foliage-component-not-named: ") + ComponentNode.Path;
                    return false;
                }
                TArray<FFoliageInstance> List;
                List.SetNum(int32(Said.Instances.size()));
                TArray<const FFoliageInstance*> Pointers;
                const FTransform ToWorld = Component->GetComponentTransform();
                for (int32 Row = 0; Row < List.Num(); ++Row)
                {
                    FMatrix Matrix;
                    FMemory::Memcpy(&Matrix, Said.Instances[Row].data(), 64);
                    const FTransform Stands = FTransform(Matrix) * ToWorld;
                    List[Row].Location = Stands.GetLocation();
                    List[Row].Rotation = Stands.Rotator();
                    List[Row].DrawScale3D = Stands.GetScale3D();
                    Pointers.Add(&List[Row]);
                }
                if (Pointers.Num())
                    Info->AddInstances(Actor, Type, Pointers);
                if (Component->PerInstanceSMData.Num() != List.Num())
                {
                    Why = TEXT("level-import-foliage-instances-not-made: ") + ComponentNode.Path;
                    return false;
                }
                Made.Add(One.Component, Component);
            }
            Planted.Add(Entry.Key);
            Progress = true;
        }
        return true;
    };
    // What the game's file says, given to everything made so far. The last pass gives it to all again
    // and has to succeed.
    const auto Give = [&](bool Final)
    {
        for (const auto& Pair : Made)
        {
            if (Pair.Key == File.World || Pair.Key == File.Level || Pair.Key == File.Model ||
                Pair.Key == File.ScriptClass || Pair.Key == File.ScriptDefault)
                continue;
            if (!Final && Given.Contains(Pair.Key))
                continue;
            FString Problem;
            if (Apply(File, Pair.Key, Pair.Value, Made, Final, Problem, &NotHeld, &NotShipped))
                Given.Add(Pair.Key);
            else if (Final)
            {
                Why = Problem;
                return false;
            }
        }
        return true;
    };
    // A component is attached by what its properties say. Its parent's list of children is the
    // engine's own and is brought in line, the components made here join the level, and the actors
    // take up their settings, which is when a component makes its child actor.
    // The engine detaches a component from the parent it names before attaching it again, and a registered
    // component its parent does not list is not attached in the engine's eyes. It is attached from nothing.
    const FObjectProperty* AttachParentProperty = FindFProperty<FObjectProperty>(
        USceneComponent::StaticClass(), TEXT("AttachParent"));
    const auto Settle = [&]()
    {
        for (const auto& Pair : Made)
            if (USceneComponent* Scene = Cast<USceneComponent>(Pair.Value))
                if (USceneComponent* Parent = Scene->GetAttachParent())
                    if (!Parent->GetAttachChildren().Contains(Scene))
                    {
                        if (Scene->IsRegistered() && AttachParentProperty)
                            AttachParentProperty->SetObjectPropertyValue_InContainer(Scene, nullptr);
                        Scene->AttachToComponent(
                            Parent, FAttachmentTransformRules::KeepRelativeTransform, Scene->GetAttachSocketName());
                    }
        for (UActorComponent* Component : Added)
            if (!Component->IsRegistered())
                Component->RegisterComponent();
        // An actor that says a component made it, while that component has another child, is left over
        // from the making. No load of a level has such an actor.
        const TArray<AActor*> All = Level->Actors;
        for (AActor* Actor : All)
        {
            UChildActorComponent* Parent = Actor && !Actor->IsPendingKill() ? Actor->GetParentComponent() : nullptr;
            if (Parent && Parent->GetChildActor() != Actor)
                World->DestroyActor(Actor);
        }
        for (const int32 Index : File.Actors)
            if (UObject** Found = Made.Find(Index))
                if (AActor* Actor = Cast<AActor>(*Found))
                    Actor->UpdateComponentTransforms();
    };
    // A component makes its child actor under the next free name. The game's file says which actor is
    // the component's, so the child is made again under that name, from the component's template.
    const FObjectProperty* ChildTemplate = FindFProperty<FObjectProperty>(
        UChildActorComponent::StaticClass(), TEXT("ChildActorTemplate"));
    const FObjectProperty* ChildLink = FindFProperty<FObjectProperty>(
        UChildActorComponent::StaticClass(), TEXT("ChildActor"));
    const ERenameFlags Quietly = REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders;
    const auto Children = [&](bool& Progress)
    {
        TArray<TPair<UChildActorComponent*, FString>> Now;
        for (const auto& Pair : Made)
        {
            UChildActorComponent* Component = Cast<UChildActorComponent>(Pair.Value);
            const FDecodedValue* Child = Component ? File.Node(Pair.Key).Values.Field("ChildActor") : nullptr;
            const FDecodedValue* ChildClass = Component ? File.Node(Pair.Key).Values.Field("ChildActorClass") : nullptr;
            if (RetireClearedInheritedChild(Component, ChildClass, Child, Named))
            {
                Progress = true;
                continue;
            }
            if (!Component || Named.Contains(Component) || !Child || Child->Kind != FDecodedValue::EKind::String)
                continue;
            FString Wanted = Text(Child->Text);
            int32 Dot = INDEX_NONE;
            if (Wanted.FindLastChar(TEXT('.'), Dot))
                Wanted = Wanted.Mid(Dot + 1);
            Now.Emplace(Component, Wanted);
        }
        if (!Now.Num())
            return;
        // Every child actor that does not have its name from the file yet steps aside first, so that no
        // name asked for is taken by another child.
        const TArray<AActor*> All = Level->Actors;
        for (AActor* Actor : All)
        {
            UChildActorComponent* Parent = Actor && !Actor->IsPendingKill() ? Actor->GetParentComponent() : nullptr;
            if (Parent && !Named.Contains(Parent))
                Actor->Rename(
                    *MakeUniqueObjectName(Level, Actor->GetClass(), TEXT("P4ChildActor")).ToString(), nullptr, Quietly);
        }
        for (const TPair<UChildActorComponent*, FString>& Pair : Now)
        {
            UChildActorComponent* Component = Pair.Key;
            Named.Add(Component);
            Progress = true;
            // The file may give the component another class of child than the component's own class does.
            // The child made before the file's settings were there is then of the wrong class. The
            // component is not asked to destroy it: it would keep what that child had and give it to
            // the next one.
            AActor* Child = Component->GetChildActor();
            if (Child && Child->GetClass() != Component->GetChildActorClass())
            {
                Child->Rename(
                    *MakeUniqueObjectName(Level, Child->GetClass(), TEXT("P4ChildActorOfAnotherClass")).ToString(),
                    nullptr, Quietly);
                World->DestroyActor(Child);
                if (ChildLink)
                    ChildLink->SetObjectPropertyValue_InContainer(Component, nullptr);
            }
            // The component's template for its child is of the component's own class of child. A child
            // of another class is made from that class as it is.
            UObject* Template = ChildTemplate ? ChildTemplate->GetObjectPropertyValue_InContainer(Component) : nullptr;
            if (Template && Template->GetClass() != Component->GetChildActorClass())
                ChildTemplate->SetObjectPropertyValue_InContainer(Component, nullptr);
            // The component keeps its child's name in a field of its own, and the engine's way of
            // putting a component back after a construction script run is what sets it: it makes the
            // child when there is none, and gives the child the name.
            FChildActorComponentInstanceData Kept(Component);
            Kept.ChildActorName = FName(*Pair.Value);
            Kept.ComponentInstanceData = nullptr;
            Kept.AttachedActors.Reset();
            Component->ApplyComponentInstanceData(&Kept, ECacheApplyPhase::PostUserConstructionScript);
        }
    };
    for (bool Progress = true; Progress;)
    {
        Progress = false;
        if (!Actors(Progress) || !Volumes(Progress) || !Foliage(Progress) || !Inside(Progress))
            return Fail(Why);
        Give(false);
        Children(Progress);
        Settle();
    }
    for (const int32 Index : File.Actors)
        if (!Made.Contains(Index))
            return Fail(TEXT("level-import-child-actor-not-made-by-its-parent: ") + File.Node(Index).Path);
    for (const auto& Pair : Made)
        if (!Pair.Value || Pair.Value->IsPendingKill())
            return Fail(TEXT("level-import-object-did-not-last: ") + File.Node(Pair.Key).Path);
    for (const auto& Entry : File.Brushes)
        if (!Made.Contains(Entry.Key))
            return Fail(TEXT("level-import-brush-shape-not-brought-over-yet: ") + File.Node(Entry.Key).Path);
    if (File.ScriptActor)
    {
        ULevelScriptBlueprint* Blueprint = Level->GetLevelScriptBlueprint(false);
        if (!Blueprint)
            return Fail(TEXT("level-import-level-script-not-made"));
        const FDecodedValue& Defaults = File.Node(File.ScriptDefault).Values;
        for (const P4MapKit::FClassVariable& Variable : File.ClassNative.Variables)
        {
            FEdGraphPinType Kind;
            Kind.PinCategory = VariableKind(Variable.Type);
            const FName Name(*Text(Variable.Name));
            const FDecodedValue* Default = Defaults.Field(Variable.Name);
            if (!FBlueprintEditorUtils::AddMemberVariable(
                    Blueprint, Name, Kind, Default ? Text(Default->Text) : FString()))
                return Fail(TEXT("level-import-level-script-variable-not-made: ") + Text(Variable.Name));
            for (FBPVariableDescription& Description : Blueprint->NewVariables)
                if (Description.VarName == Name)
                    Description.PropertyFlags = Variable.PropertyFlags;
        }
        if (File.ProvenEmptyBeginPlay)
        {
            UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
            UFunction* Signature = ALevelScriptActor::StaticClass()->FindFunctionByName(TEXT("ReceiveBeginPlay"));
            if (!Graph || !Signature)
                return Fail(TEXT("level-import-empty-begin-play-has-no-event-graph"));
            UK2Node_Event* Event = nullptr;
            for (UEdGraphNode* Each : Graph->Nodes)
                if (UK2Node_Event* Candidate = Cast<UK2Node_Event>(Each))
                    if (Candidate->EventReference.GetMemberName() == Signature->GetFName())
                        Event = Candidate;
            if (!Event)
            {
                Event = NewObject<UK2Node_Event>(Graph, NAME_None, RF_Transactional);
                Event->EventReference.SetFromField<UFunction>(Signature, false);
                Event->bOverrideFunction = true;
                Graph->AddNode(Event, false, false);
                Event->CreateNewGuid();
                Event->AllocateDefaultPins();
            }
            Event->MakeAutomaticallyPlacedGhostNode();
            Event->bCommentBubbleVisible = false;
            Event->SetEnabledState(ENodeEnabledState::Enabled, true);
            Event->NodeComment.Reset();
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        UClass* Generated = Blueprint->GeneratedClass;
        const FString Wanted = File.PackageName + TEXT(".") + File.Node(File.ScriptClass).Path;
        if (Blueprint->Status == BS_Error || !Generated || !P4SameText(Generated->GetPathName(), Wanted))
            return Fail(
                FString::Printf(TEXT("level-import-level-script-class-not-made: status %d, class %s, wanted %s"),
                    int32(Blueprint->Status), Generated ? *Generated->GetPathName() : TEXT("none"), *Wanted));
        ALevelScriptActor* Actor = Level->GetLevelScriptActor();
        if (!Actor || Actor->GetClass() != Generated)
            return Fail(TEXT("level-import-level-script-actor-not-made"));
        // Compiling the script again makes its actor again, under the next name.
        const FString ActorName = File.Node(File.ScriptActor).Name;
        const ERenameFlags Quiet = REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders;
        if (!P4SameText(Actor->GetName(), ActorName))
        {
            if (UObject* Old = StaticFindObjectFast(nullptr, Level, FName(*ActorName)))
                Old->Rename(nullptr, GetTransientPackage(), Quiet);
            if (!Actor->Rename(*ActorName, nullptr, Quiet))
                return Fail(TEXT("level-import-level-script-actor-not-named"));
        }
        Made.Add(File.ScriptClass, Generated);
        Made.Add(File.ScriptDefault, Generated->GetDefaultObject());
        Made.Add(File.ScriptActor, Actor);
    }
    // A component that a class's construction script makes and the game's file does not have for the
    // actor: the project's Blueprint makes more than the game's did. It is taken out, so that the level
    // is what the game's file says. A construction script run makes it again, and that is told with
    // what such a run adds.
    {
        TSet<UObject*> Known;
        for (const auto& Pair : Made)
            Known.Add(Pair.Value);
        for (const int32 Index : File.Actors)
            if (AActor* Actor = Cast<AActor>(Made.FindRef(Index)))
            {
                TInlineComponentArray<UActorComponent*> Components(Actor);
                for (UActorComponent* Component : Components)
                    // Not the sprite the editor shows a component by: that is never saved.
                    if (Component && Component->IsCreatedByConstructionScript() && !Known.Contains(Component) &&
                        !Component->HasAnyFlags(RF_Transient) && !Component->IsEditorOnly())
                    {
                        NotInGame.Add(File.Node(Index).Path + TEXT(".") + Component->GetName());
                        Component->DestroyComponent();
                    }
            }
    }
    NotHeld.Reset();
    NotShipped.Reset();
    if (!Give(true))
        return Fail(Why);
    Settle();
    // The objects lazy references name get the ids the file says they carry, and a landscape's height maps their
    // images. Giving an object its values gives its source image back to the class's, so this comes after.
    for (const auto& Pair : Made)
    {
        const FNode& Node = File.Node(Pair.Key);
        if (!Node.ObjectGuid.IsEmpty() && !GiveObjectGuid(Pair.Value, Node.ObjectGuid))
            return Fail(TEXT("level-import-object-id-held-by-another-object: ") + Node.Path);
        if (!Node.CookedTexture)
            continue;
        UTexture* Texture = Cast<UTexture>(Pair.Value);
        FString TextureError;
        if (!Texture ||
            !P4ReadPackageTextureImage(File.Package, File.Body, TCHAR_TO_UTF8(*Node.Class), Node.TailStart,
                Node.TailEnd, Texture, TextureError))
            return Fail(TEXT("level-import-texture-image-unreadable: ") + Node.Path + TEXT(": ") + TextureError);
        Texture->UpdateResource();
    }
    // A landscape knows its parts once it has its record (ULandscapeInfo), which the engine makes when the
    // landscape is registered with its id, before the import gave it the id. Each landscape component then
    // works out its collision component's heights from its height map, as the landscape editor does, which a
    // cook makes the collision from. That gives the collision component a new id for its heights: it gets back
    // what the file says.
    TArray<int32> Collisions;
    for (const auto& Pair : Made)
        if (ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(Pair.Value))
            Landscape->CreateLandscapeInfo();
    for (const auto& Pair : Made)
    {
        ULandscapeComponent* Component = Cast<ULandscapeComponent>(Pair.Value);
        if (Pair.Value->IsA<ULandscapeHeightfieldCollisionComponent>())
            Collisions.Add(Pair.Key);
        if (!Component)
            continue;
        FString Problem;
        if (!GiveLandscapeCollisionHeights(Component, Problem))
            return Fail(Problem + TEXT(": ") + File.Node(Pair.Key).Path);
    }
    for (const int32 Index : Collisions)
        if (!Apply(File, Index, Made[Index], Made, true, Why, &NotHeld, &NotShipped))
            return Fail(Why);
    // First let the engine establish its render resources. The game's cooked
    // instance order and culling tree are then restored together: rebuilding
    // uses this editor's mesh/LOD heuristics and can produce another order.
    static_assert(sizeof(FMatrix) == 64, "an instance is four rows of four reals");
    for (const auto& Entry : File.Instanced)
    {
        const P4MapKit::FInstancedMeshNative& Said = Entry.Value;
        UHierarchicalInstancedStaticMeshComponent* Component = Cast<UHierarchicalInstancedStaticMeshComponent>(
            Made.FindRef(Entry.Key));
        if (!Component || Component->PerInstanceSMData.Num() != int32(Said.Instances.size()))
            return Fail(TEXT("level-import-instances-not-made: ") + File.Node(Entry.Key).Path);
        for (int32 Row = 0; Row < Component->PerInstanceSMData.Num(); ++Row)
            FMemory::Memcpy(&Component->PerInstanceSMData[Row].Transform, Said.Instances[Row].data(), 64);
        Component->PerInstanceSMCustomData.Reset();
        Component->PerInstanceSMCustomData.Append(Said.Custom.data(), int32(Said.Custom.size()));
        Component->BuildTreeIfOutdated(false, true);
        if (!Apply(File, Entry.Key, Component, Made, true, Why, &NotHeld, &NotShipped))
            return Fail(Why);
        if (Said.DrawnBytes)
        {
            if (Said.Drawn < 0 || Said.Origins.size() != std::size_t(Said.Drawn) ||
                Said.Rows.size() != std::size_t(Said.Drawn) || Said.LightMaps.size() != std::size_t(Said.Drawn) ||
                Said.DrawnCustom.size() != std::size_t(Said.Drawn) * Said.CustomPerInstance ||
                Component->SortedInstances.Num() != Said.Drawn ||
                Component->InstanceReorderTable.Num() != Component->PerInstanceSMData.Num())
                return Fail(TEXT("level-import-instance-render-counts-differ: ") + File.Node(Entry.Key).Path);
            FStaticMeshInstanceData Drawn(Said.HalfReals);
            Drawn.AllocateInstances(Said.Drawn, Said.CustomPerInstance, EResizeBufferFlags::None, true);
            Drawn.SetAllowCPUAccess(true);
            const auto Copy = [&](FResourceArrayInterface* Buffer, const void* Data, std::size_t Bytes)
            {
                if (Buffer->GetResourceDataSize() != Bytes)
                    return false;
                if (Bytes)
                    FMemory::Memcpy(const_cast<void*>(Buffer->GetResourceData()), Data, Bytes);
                return true;
            };
            bool Copied = Copy(Drawn.GetOriginResourceArray(), Said.Origins.data(), Said.Origins.size() * 16) &&
                Copy(Drawn.GetLightMapResourceArray(), Said.LightMaps.data(), Said.LightMaps.size() * 8) &&
                Copy(Drawn.GetCustomDataResourceArray(), Said.DrawnCustom.data(), Said.DrawnCustom.size() * 4);
            if (Said.HalfReals)
            {
                TArray<FFloat16> Rows;
                Rows.Reserve(Said.Drawn * 12);
                for (const auto& Row : Said.Rows)
                    for (const float Part : Row)
                        Rows.Add(FFloat16(Part));
                Copied = Copied &&
                    Copy(Drawn.GetTransformResourceArray(), Rows.GetData(), Rows.Num() * sizeof(FFloat16));
            }
            else
                Copied = Copied && Copy(Drawn.GetTransformResourceArray(), Said.Rows.data(), Said.Rows.size() * 48);
            if (!Copied)
                return Fail(TEXT("level-import-instance-render-layout-differs: ") + File.Node(Entry.Key).Path);
            Component->ReleasePerInstanceRenderData();
            Component->InitPerInstanceRenderData(false, &Drawn, true);
        }
        Component->ClusterTreePtr = MakeShareable(new TArray<FClusterNode>);
        for (const auto& Source : Said.Clusters)
        {
            FClusterNode Cluster;
            Cluster.BoundMin = FVector(Source.Min[0], Source.Min[1], Source.Min[2]);
            Cluster.BoundMax = FVector(Source.Max[0], Source.Max[1], Source.Max[2]);
            Cluster.FirstChild = Source.FirstChild;
            Cluster.LastChild = Source.LastChild;
            Cluster.FirstInstance = Source.FirstInstance;
            Cluster.LastInstance = Source.LastInstance;
            Cluster.MinInstanceScale = FVector(Source.MinScale[0], Source.MinScale[1], Source.MinScale[2]);
            Cluster.MaxInstanceScale = FVector(Source.MaxScale[0], Source.MaxScale[1], Source.MaxScale[2]);
            Component->ClusterTreePtr->Add(Cluster);
        }
        Component->BuiltInstanceBounds = Said.Clusters.empty() ? FBox(ForceInit)
                                                               : FBox(Component->ClusterTreePtr->operator[](0).BoundMin,
                                                                     Component->ClusterTreePtr->operator[](0).BoundMax);
        Component->MarkRenderStateDirty();
    }
    // The level's actors in the game's order, the builder brush where the game's file has its gap.
    TArray<AActor*> Order;
    for (const std::int32_t Index : File.LevelNative.Actors)
        if (UObject** Found = Made.Find(Index))
            Order.AddUnique(Cast<AActor>(*Found));
    if (Brush && Order.Num() >= 1)
        Order.Insert(Brush, 1);
    for (AActor* Actor : Level->Actors)
        if (Actor && !Order.Contains(Actor))
            Order.Add(Actor);
    Level->Actors = Order;
    // What the game's editor held for the level after loading it: the address the level was last saved under, its
    // lighting build's id, its texture streaming build's list, the model's lighting id and each object's saved flags.
    const P4MapKit::FLevelNative& Native = File.LevelNative;
    Level->URL.Protocol = Text(Native.Protocol);
    Level->URL.Host = Text(Native.Host);
    Level->URL.Map = Text(Native.Map);
    Level->URL.Portal = Text(Native.Portal);
    Level->URL.Op.Reset();
    for (const std::string& Option : Native.Options)
        Level->URL.Op.Add(Text(Option));
    Level->URL.Port = Native.Port;
    Level->URL.Valid = Native.Valid;
    const auto Guid = [](const FDecodedValue* Value, FGuid& Out)
    {
        if (!Value || Value->Kind != FDecodedValue::EKind::Object || Value->Fields.size() != 4)
            return false;
        uint32 Words[4];
        int32 Count = 0;
        for (const char* Name : {"A", "B", "C", "D"})
        {
            const FDecodedValue* Part = Value->Field(Name);
            if (!Part || Part->Kind != FDecodedValue::EKind::Number)
                return false;
            Words[Count++] = uint32(FCString::Strtoi64(*Text(Part->Text), nullptr, 10));
        }
        Out = FGuid(Words[0], Words[1], Words[2], Words[3]);
        return true;
    };
    FGuid BuildId;
    if (Guid(File.Node(File.Level).Values.Field("LevelBuildDataId"), BuildId))
        Level->LevelBuildDataId = BuildId;
    // The textures of the level's texture streaming build, which each component's streaming data names by its place
    // in this list.
    if (const FDecodedValue* Textures = File.Node(File.Level).Values.Field("StreamingTextureGuids"))
        if (Textures->Kind == FDecodedValue::EKind::Array)
        {
            TArray<FGuid> Read;
            for (const FDecodedValue& Item : Textures->Items)
                if (Guid(&Item, BuildId))
                    Read.Add(BuildId);
            if (Read.Num() == int32(Textures->Items.size()))
                Level->StreamingTextureGuids = MoveTemp(Read);
        }
    if (Level->Model)
        Level->Model->LightingGuid = FGuid(File.ModelNative.LightingGuid[0], File.ModelNative.LightingGuid[1],
            File.ModelNative.LightingGuid[2], File.ModelNative.LightingGuid[3]);
    for (const auto& Pair : Made)
        if (Pair.Key != File.ScriptClass && Pair.Key != File.ScriptDefault && Pair.Value)
        {
            const EObjectFlags Wanted = EObjectFlags(File.Node(Pair.Key).Flags) & SavedFlags;
            Pair.Value->ClearFlags(SavedFlags & ~Wanted);
            Pair.Value->SetFlags(Wanted);
        }
    if (!P4OwnStockEngineFields(World->GetOutermost(), Error))
        return Fail(Error);
    return World;
}
} // namespace P4LevelContent
